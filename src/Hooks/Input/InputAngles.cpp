#include "InputAngles.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "../../Portal/L4D2_Portal.h"
#include "../../Portal/PortalTransitionDecision.h"
#include "../../Portal/PortalViewReorientation.h"
#include "../../SDK/L4D2/Interfaces/EngineClient.h"
#include "../../SDK/L4D2/Interfaces/IInput.h"
#include "../../Util/Logger/Logger.h"
#include "../../Util/Offsets/Offsets.h"

namespace
{
    struct ValidationResult
    {
        bool moduleValid = false;
        bool pe32Valid = false;
        bool imageRangeValid = false;
        bool codeRangeValid = false;
        bool executable = false;
        bool rollLoadValid = false;
        bool positiveLoadValid = false;
        bool negativeLoadValid = false;
        bool compareStoreValid = false;
        bool positiveConstantReadable = false;
        bool negativeConstantReadable = false;
        bool positiveConstantValid = false;
        bool negativeConstantValid = false;
        bool tailValid = false;
        std::uintptr_t codeStart = 0;
        std::uintptr_t codeEnd = 0;
        std::uintptr_t imageEnd = 0;
        std::uintptr_t positiveConstantAddress = 0;
        std::uintptr_t negativeConstantAddress = 0;
        float positiveConstant = 0.0f;
        float negativeConstant = 0.0f;

        bool IsValid() const
        {
            return moduleValid && codeRangeValid && executable
                && rollLoadValid && positiveLoadValid && negativeLoadValid
                && compareStoreValid
                && positiveConstantReadable && negativeConstantReadable
                && positiveConstantValid && negativeConstantValid
                && tailValid;
        }
    };

    bool g_signatureValidated = false;
    bool g_abiValidated = false;
    bool g_abiCheckCompleted = false;
    bool g_initAttempted = false;
    bool g_initCompleted = false;
    bool g_hookCreateAttempted = false;
    bool g_hookCreated = false;
    std::uintptr_t g_initialTarget = 0;
    std::uint32_t g_lastLoggedTransactionId = 0;

    const char* BoolText(bool value)
    {
        return value ? "true" : "false";
    }

    bool IsExecutableProtection(DWORD protection)
    {
        switch (protection & 0xFFu)
        {
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

    bool IsReadableProtection(DWORD protection)
    {
        switch (protection & 0xFFu)
        {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

    bool IsReadableRange(std::uintptr_t address, std::size_t size)
    {
        if (!address || !size || address > UINTPTR_MAX - size)
            return false;

        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region))
            return false;

        const std::uintptr_t regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (regionStart > UINTPTR_MAX - region.RegionSize)
            return false;
        const std::uintptr_t regionEnd = regionStart + region.RegionSize;
        return region.State == MEM_COMMIT
            && (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0
            && IsReadableProtection(region.Protect)
            && address >= regionStart
            && address + size <= regionEnd;
    }

    bool TryAdd(std::uintptr_t base, std::size_t offset, std::uintptr_t* result)
    {
        if (!result || base > UINTPTR_MAX - offset)
            return false;

        *result = base + offset;
        return true;
    }

    template <typename T>
    bool TryRead(std::uintptr_t address, T* value)
    {
        if (!value || !IsReadableRange(address, sizeof(T)))
            return false;

        std::memcpy(value, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }

    bool Matches(std::uintptr_t address, const unsigned char* expected, std::size_t size)
    {
        return IsReadableRange(address, size)
            && std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
    }

    bool TryReadAbsoluteFloatOperand(std::uintptr_t instruction, std::uintptr_t* address, float* value)
    {
        if (!address || !value || !IsReadableRange(instruction + 4u, sizeof(std::uint32_t)))
            return false;

        std::uint32_t operand = 0;
        std::memcpy(&operand, reinterpret_cast<const void*>(instruction + 4u), sizeof(operand));
        *address = static_cast<std::uintptr_t>(operand);
        if (!IsReadableRange(*address, sizeof(float)))
            return false;

        std::memcpy(value, reinterpret_cast<const void*>(*address), sizeof(float));
        return true;
    }

    ValidationResult ValidateTarget(std::uintptr_t target)
    {
        ValidationResult result;
        const HMODULE module = GetModuleHandleA("client.dll");
        if (!module)
            return result;

        const std::uintptr_t moduleBase = reinterpret_cast<std::uintptr_t>(module);
        IMAGE_DOS_HEADER dos = {};
        if (!TryRead(moduleBase, &dos)
            || dos.e_magic != IMAGE_DOS_SIGNATURE
            || dos.e_lfanew < 0)
            return result;

        std::uintptr_t ntAddress = 0;
        if (!TryAdd(
                moduleBase,
                static_cast<std::size_t>(dos.e_lfanew),
                &ntAddress))
            return result;

        DWORD signature = 0;
        if (!TryRead(ntAddress, &signature)
            || signature != IMAGE_NT_SIGNATURE)
        {
            return result;
        }

        std::uintptr_t fileHeaderAddress = 0;
        IMAGE_FILE_HEADER fileHeader = {};
        if (!TryAdd(ntAddress, sizeof(signature), &fileHeaderAddress)
            || !TryRead(fileHeaderAddress, &fileHeader))
        {
            return result;
        }

        std::uintptr_t optionalHeaderAddress = 0;
        if (!TryAdd(fileHeaderAddress, sizeof(fileHeader), &optionalHeaderAddress)
            || fileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32)
            || !IsReadableRange(
                optionalHeaderAddress,
                fileHeader.SizeOfOptionalHeader))
        {
            return result;
        }

        IMAGE_OPTIONAL_HEADER32 optionalHeader = {};
        if (fileHeader.Machine != IMAGE_FILE_MACHINE_I386
            || !TryRead(optionalHeaderAddress, &optionalHeader)
            || optionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
            || optionalHeader.SizeOfImage < sizeof(IMAGE_DOS_HEADER)
            || optionalHeader.SizeOfHeaders == 0
            || optionalHeader.SizeOfHeaders > optionalHeader.SizeOfImage)
        {
            return result;
        }
        result.pe32Valid = true;

        if (!TryAdd(
                moduleBase,
                optionalHeader.SizeOfImage,
                &result.imageEnd))
        {
            return result;
        }

        std::uintptr_t optionalHeaderEnd = 0;
        if (!TryAdd(
                optionalHeaderAddress,
                fileHeader.SizeOfOptionalHeader,
                &optionalHeaderEnd)
            || ntAddress < moduleBase
            || optionalHeaderEnd > result.imageEnd
            || optionalHeaderEnd > moduleBase + optionalHeader.SizeOfHeaders)
        {
            return result;
        }
        result.imageRangeValid = true;

        if (optionalHeader.BaseOfCode >= optionalHeader.SizeOfImage
            || optionalHeader.SizeOfCode == 0
            || !TryAdd(moduleBase, optionalHeader.BaseOfCode, &result.codeStart)
            || !TryAdd(result.codeStart, optionalHeader.SizeOfCode, &result.codeEnd)
            || result.codeStart < moduleBase
            || result.codeEnd > result.imageEnd)
        {
            return result;
        }

        result.moduleValid = true;
        constexpr std::size_t kRequiredTargetBytes = 213u;
        result.codeRangeValid = target >= result.codeStart
            && target <= UINTPTR_MAX - kRequiredTargetBytes
            && target + kRequiredTargetBytes <= result.codeEnd;
        if (!result.codeRangeValid)
            return result;

        MEMORY_BASIC_INFORMATION region = {};
        const bool queried = VirtualQuery(
            reinterpret_cast<const void*>(target),
            &region,
            sizeof(region)) == sizeof(region);
        if (queried)
        {
            const std::uintptr_t regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            const bool regionRangeValid = regionStart <= UINTPTR_MAX - region.RegionSize;
            const std::uintptr_t regionEnd = regionRangeValid
                ? regionStart + region.RegionSize
                : 0;
            result.executable = regionRangeValid
                && region.State == MEM_COMMIT
                && (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0
                && IsExecutableProtection(region.Protect)
                && target >= regionStart
                && target + kRequiredTargetBytes <= regionEnd;
        }
        if (!result.executable)
            return result;

        constexpr unsigned char kRollLoad[] = { 0xF3, 0x0F, 0x10, 0x45, 0xFC };
        constexpr unsigned char kConstantLoad[] = { 0xF3, 0x0F, 0x10, 0x0D };
        constexpr unsigned char kCompareStore[] = {
            0x0F, 0x2F, 0xC8, 0x76, 0x05, 0xF3, 0x0F, 0x11, 0x4D, 0xFC
        };
        constexpr unsigned char kTail[] = { 0xC2, 0x08, 0x00 };

        result.rollLoadValid = Matches(target + 152u, kRollLoad, sizeof(kRollLoad));
        result.positiveLoadValid = Matches(target + 157u, kConstantLoad, sizeof(kConstantLoad));
        result.negativeLoadValid = Matches(target + 170u, kConstantLoad, sizeof(kConstantLoad));
        result.compareStoreValid = Matches(target + 178u, kCompareStore, sizeof(kCompareStore));
        result.tailValid = Matches(target + 210u, kTail, sizeof(kTail));

        if (result.positiveLoadValid)
        {
            result.positiveConstantReadable = TryReadAbsoluteFloatOperand(
                target + 157u,
                &result.positiveConstantAddress,
                &result.positiveConstant);
            result.positiveConstantValid = result.positiveConstantReadable
                && std::isfinite(result.positiveConstant)
                && std::fabs(result.positiveConstant - 50.0f) <= 0.001f;
        }
        if (result.negativeLoadValid)
        {
            result.negativeConstantReadable = TryReadAbsoluteFloatOperand(
                target + 170u,
                &result.negativeConstantAddress,
                &result.negativeConstant);
            result.negativeConstantValid = result.negativeConstantReadable
                && std::isfinite(result.negativeConstant)
                && std::fabs(result.negativeConstant + 50.0f) <= 0.001f;
        }

        return result;
    }

    std::uintptr_t ResolveLateTarget()
    {
        return static_cast<std::uintptr_t>(U::Pattern.Find(
            "client.dll",
            "55 8B EC 83 EC 0C D9 45 0C 56 57 8B 7D 08 51 D9 1C 24 57 8B F1 E8 ? ? ? ? D9 55 0C D9 EE DF F1 DD D8 0F 83 ? ? ? ? 8B 0D ? ? ? ? 8B 01 8B 40 4C 8D 55 F4 52 FF D0"));
    }

    void LogProbeValidation(
        const char* source,
        std::uintptr_t target,
        const ValidationResult& validation)
    {
        U::PortalFileLog::WriteAlwaysFormat(
            "[PortalInputAdjustAnglesProbe] stage=validation source=%s target=%p imageEnd=%p code=[%p,%p) pe32=%s imageRange=%s module=%s codeRange=%s executable=%s opcodes(roll=%s plusLoad=%s minusLoad=%s compareStore=%s tailRet8=%s) constants(plusReadable=%s plusAddress=%p plus=%.3f plusValid=%s minusReadable=%s minusAddress=%p minus=%.3f minusValid=%s) valid=%s mutation=false.\n",
            source ? source : "unknown",
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(validation.imageEnd),
            reinterpret_cast<void*>(validation.codeStart),
            reinterpret_cast<void*>(validation.codeEnd),
            BoolText(validation.pe32Valid),
            BoolText(validation.imageRangeValid),
            BoolText(validation.moduleValid),
            BoolText(validation.codeRangeValid),
            BoolText(validation.executable),
            BoolText(validation.rollLoadValid),
            BoolText(validation.positiveLoadValid),
            BoolText(validation.negativeLoadValid),
            BoolText(validation.compareStoreValid),
            BoolText(validation.tailValid),
            BoolText(validation.positiveConstantReadable),
            reinterpret_cast<void*>(validation.positiveConstantAddress),
            validation.positiveConstant,
            BoolText(validation.positiveConstantValid),
            BoolText(validation.negativeConstantReadable),
            reinterpret_cast<void*>(validation.negativeConstantAddress),
            validation.negativeConstant,
            BoolText(validation.negativeConstantValid),
            BoolText(validation.IsValid()));
    }
}

bool Hooks::InputAngles::IsSignatureValidated()
{
    return g_signatureValidated;
}

bool Hooks::InputAngles::IsAbiValidated()
{
    return g_abiValidated;
}

void Hooks::InputAngles::LogLateDiagnosticProbe()
{
    const std::uintptr_t storedTarget =
        static_cast<std::uintptr_t>(U::Offsets.m_dwInputAdjustAngles);
    const std::uintptr_t lateTarget = ResolveLateTarget();
    const ValidationResult storedValidation = ValidateTarget(storedTarget);
    const ValidationResult lateValidation = ValidateTarget(lateTarget);

    U::PortalFileLog::WriteAlwaysFormat(
        "[PortalInputAdjustAnglesProbe] stage=summary initAttempted=%s initCompleted=%s initialTarget=%p storedTarget=%p lateTarget=%p initialMatchesStored=%s storedMatchesLate=%s hookCreateAttempted=%s hookCreated=%s signatureValidated=%s abiCheckCompleted=%s abiValidated=%s inputInstance=%p mutation=false.\n",
        BoolText(g_initAttempted),
        BoolText(g_initCompleted),
        reinterpret_cast<void*>(g_initialTarget),
        reinterpret_cast<void*>(storedTarget),
        reinterpret_cast<void*>(lateTarget),
        BoolText(g_initialTarget != 0 && g_initialTarget == storedTarget),
        BoolText(storedTarget != 0 && storedTarget == lateTarget),
        BoolText(g_hookCreateAttempted),
        BoolText(g_hookCreated),
        BoolText(g_signatureValidated),
        BoolText(g_abiCheckCompleted),
        BoolText(g_abiValidated),
        reinterpret_cast<void*>(I::IInput));
    LogProbeValidation("stored", storedTarget, storedValidation);
    LogProbeValidation("late-rescan", lateTarget, lateValidation);
}

void Hooks::InputAngles::Init()
{
    g_initAttempted = true;
    g_initCompleted = false;
    g_signatureValidated = false;
    g_abiValidated = false;
    g_abiCheckCompleted = false;
    g_hookCreateAttempted = false;
    g_hookCreated = false;
    g_lastLoggedTransactionId = 0;

    const std::uintptr_t target = static_cast<std::uintptr_t>(U::Offsets.m_dwInputAdjustAngles);
    g_initialTarget = target;
    if (!target)
    {
        U::LogError("[PortalInputAdjustAnglesHook] target=CInput::AdjustAngles installed=false reason=signature-missing mutation=false.\n");
        g_initCompleted = true;
        return;
    }

    const ValidationResult validation = ValidateTarget(target);
    g_signatureValidated = validation.IsValid();
    U::LogInfo(
        "[PortalInputAdjustAnglesHook] target=CInput::AdjustAngles address=%p imageEnd=%p code=[%p,%p) pe32=%s imageRange=%s module=%s codeRange=%s executable=%s opcodes(roll=%s plusLoad=%s minusLoad=%s compareStore=%s tailRet8=%s) constants(plusReadable=%s plusAddress=%p plus=%.3f plusValid=%s minusReadable=%s minusAddress=%p minus=%.3f minusValid=%s) signatureValidated=%s mutation=false.\n",
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(validation.imageEnd),
        reinterpret_cast<void*>(validation.codeStart),
        reinterpret_cast<void*>(validation.codeEnd),
        BoolText(validation.pe32Valid),
        BoolText(validation.imageRangeValid),
        BoolText(validation.moduleValid),
        BoolText(validation.codeRangeValid),
        BoolText(validation.executable),
        BoolText(validation.rollLoadValid),
        BoolText(validation.positiveLoadValid),
        BoolText(validation.negativeLoadValid),
        BoolText(validation.compareStoreValid),
        BoolText(validation.tailValid),
        BoolText(validation.positiveConstantReadable),
        reinterpret_cast<void*>(validation.positiveConstantAddress),
        validation.positiveConstant,
        BoolText(validation.positiveConstantValid),
        BoolText(validation.negativeConstantReadable),
        reinterpret_cast<void*>(validation.negativeConstantAddress),
        validation.negativeConstant,
        BoolText(validation.negativeConstantValid),
        BoolText(g_signatureValidated));
    if (!g_signatureValidated)
    {
        U::LogError("[PortalInputAdjustAnglesHook] target=CInput::AdjustAngles installed=false reason=runtime-validation-failed mutation=false.\n");
        g_initCompleted = true;
        return;
    }

    g_hookCreateAttempted = true;
    const bool created = AdjustAngles::Func.Init(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&AdjustAngles::Detour));
    g_hookCreated = created;
    U::LogInfo("[PortalInputAdjustAnglesHook] target=CInput::AdjustAngles installed=%s signatureValidated=true abiValidated=false mutation=false.\n",
        BoolText(created));
    if (!created)
        U::LogError("[PortalInputAdjustAnglesHook] target=CInput::AdjustAngles installed=false reason=MH_CreateHook-failed mutation=false.\n");
    g_initCompleted = true;
}

void __fastcall Hooks::InputAngles::AdjustAngles::Detour(
    void* ecx,
    void* edx,
    int slot,
    float frametime)
{
    (void)edx;

    if (!g_abiCheckCompleted)
    {
        g_abiCheckCompleted = true;
        g_abiValidated = ecx != nullptr && ecx == I::IInput;
        if (g_abiValidated)
        {
            U::LogInfo("[PortalInputAdjustAnglesHook] abiValidated=true this=%p expected=%p mutation=false.\n",
                ecx, I::IInput);
        }
        else
        {
            U::LogError("[PortalInputAdjustAnglesHook] abiValidated=false this=%p expected=%p reason=first-call-instance-mismatch mutation=false.\n",
                ecx, I::IInput);
        }
    }

    Vector before;
    const bool engineAvailable = I::EngineClient != nullptr;
    if (engineAvailable)
        I::EngineClient->GetViewAngles(before);

    Func.Original<FN>()(ecx, slot, frametime);

    if (!engineAvailable)
        return;

    Vector after;
    I::EngineClient->GetViewAngles(after);
    CPortalTransition& transition = G::G_L4D2Portal.m_PortalTransition;
    const bool restore = PortalViewReorientation::ShouldRestoreInputRoll(
        PortalTransitionDecision::GetPortalViewReorientationEnabled(),
        g_signatureValidated,
        g_abiValidated,
        ecx == I::IInput,
        transition.ShouldPreservePortalInputRoll());
    if (!restore)
        return;

    const float clampedRoll = after.z;
    after.z = before.z;
    I::EngineClient->SetViewAngles(after);

    const std::uint32_t transactionId = transition.GetPortalViewReorientationTransactionId();
    if (transactionId != g_lastLoggedTransactionId)
    {
        g_lastLoggedTransactionId = transactionId;
        U::LogInfo("[PortalInputRollClamp] txn=%u slot=%d frametime=%.6f before=%.3f clamped=%.3f restored=%.3f scope=view-angles-only physicsMutation=false.\n",
            transactionId,
            slot,
            frametime,
            before.z,
            clampedRoll,
            after.z);
    }
}
