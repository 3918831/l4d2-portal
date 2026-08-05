#include "ServerPlayerRunCommand.h"

#include "../../Portal/L4D2_Portal.h"
#include "../../Portal/PortalPlayerTeleport.h"
#include "../../Portal/PortalTransitionSimulator.h"
#include "../../SDK/L4D2/Entities/C_TerrorPlayer.h"
#include "../../SDK/L4D2/Includes/usercmd.h"
#include "../../SDK/L4D2/Server/ServerDataMap.h"
#include "../../Util/Logger/Logger.h"

#include <Windows.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

using namespace Hooks;

namespace
{
    std::atomic<void*> g_localServerPlayer = nullptr;
    void* g_hookTarget = nullptr;
    void* g_lastAttemptedServerPlayer = nullptr;
    bool g_hookInstalled = false;
    std::uint32_t g_localCallSequence = 0;
    thread_local CUserCmd* g_activeServerCommand = nullptr;

    struct ServerFieldCache
    {
        CBaseEntity* entity = nullptr;
        L4D2::ServerDataMap::FieldInfo origin;
        L4D2::ServerDataMap::FieldInfo velocity;
        L4D2::ServerDataMap::FieldInfo moveType;
        bool originResolved = false;
        bool velocityResolved = false;
        bool moveTypeResolved = false;
    };

    struct ServerStateSnapshot
    {
        Vector origin;
        Vector velocity;
        int moveType = -1;
        bool originValid = false;
        bool velocityValid = false;
        bool moveTypeValid = false;
    };

    struct MemoryRegionSnapshot
    {
        void* baseAddress = nullptr;
        void* allocationBase = nullptr;
        std::size_t regionSize = 0;
        DWORD state = 0;
        DWORD protect = 0;
        DWORD type = 0;
        bool queried = false;
        bool executable = false;
    };

    struct ModuleImageSnapshot
    {
        HMODULE module = nullptr;
        std::uintptr_t base = 0;
        std::size_t imageSize = 0;
        char path[MAX_PATH] = {};
        bool valid = false;
    };

    struct CleanVTableTarget
    {
        void* target = nullptr;
        std::uintptr_t vtableSlotRva = 0;
        std::uintptr_t targetRva = 0;
        std::uint32_t preferredTarget = 0;
        bool resolved = false;
    };

    ServerFieldCache g_serverFields;

    bool IsReadableRange(const void* address, std::size_t size)
    {
        if (!address || size == 0)
            return false;

        MEMORY_BASIC_INFORMATION info = {};
        if (!VirtualQuery(address, &info, sizeof(info)))
            return false;

        const std::uintptr_t start =
            reinterpret_cast<std::uintptr_t>(address);
        const std::uintptr_t end = start + size - 1u;
        const std::uintptr_t regionEnd =
            reinterpret_cast<std::uintptr_t>(info.BaseAddress)
            + info.RegionSize;
        return info.State == MEM_COMMIT
            && !(info.Protect & PAGE_NOACCESS)
            && !(info.Protect & PAGE_GUARD)
            && end < regionEnd;
    }

    bool IsExecutableProtection(DWORD protection)
    {
        const DWORD baseProtection = protection & 0xFFu;
        return baseProtection == PAGE_EXECUTE
            || baseProtection == PAGE_EXECUTE_READ
            || baseProtection == PAGE_EXECUTE_READWRITE
            || baseProtection == PAGE_EXECUTE_WRITECOPY;
    }

    MemoryRegionSnapshot QueryMemoryRegion(const void* address)
    {
        MemoryRegionSnapshot result;
        if (!address)
            return result;

        MEMORY_BASIC_INFORMATION info = {};
        if (!VirtualQuery(address, &info, sizeof(info)))
            return result;

        result.baseAddress = info.BaseAddress;
        result.allocationBase = info.AllocationBase;
        result.regionSize = info.RegionSize;
        result.state = info.State;
        result.protect = info.Protect;
        result.type = info.Type;
        result.queried = true;
        result.executable =
            info.State == MEM_COMMIT
            && !(info.Protect & PAGE_GUARD)
            && IsExecutableProtection(info.Protect);
        return result;
    }

    bool TryGetModuleSnapshot(
        const void* address,
        ModuleImageSnapshot* result)
    {
        if (!address || !result)
            return false;

        ModuleImageSnapshot local;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address),
                &local.module)
            || !local.module)
        {
            return false;
        }

        local.base = reinterpret_cast<std::uintptr_t>(local.module);
        const auto* dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(local.module);
        if (!IsReadableRange(dos, sizeof(*dos))
            || dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return false;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
            local.base + static_cast<std::uintptr_t>(dos->e_lfanew));
        if (!IsReadableRange(nt, sizeof(*nt))
            || nt->Signature != IMAGE_NT_SIGNATURE
            || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            return false;
        }

        local.imageSize = nt->OptionalHeader.SizeOfImage;
        GetModuleFileNameA(local.module, local.path, MAX_PATH);
        local.valid = local.imageSize != 0;
        *result = local;
        return local.valid;
    }

    bool TryGetModuleRva(
        const ModuleImageSnapshot& module,
        const void* address,
        std::uintptr_t* rva)
    {
        if (!module.valid || !address)
            return false;

        const std::uintptr_t value =
            reinterpret_cast<std::uintptr_t>(address);
        if (value < module.base
            || value >= module.base + module.imageSize)
        {
            return false;
        }

        if (rva)
            *rva = value - module.base;
        return true;
    }

    bool IsExecutableModuleAddress(
        const ModuleImageSnapshot& module,
        void* address,
        std::uintptr_t* rva)
    {
        if (!TryGetModuleRva(module, address, rva))
            return false;

        return QueryMemoryRegion(address).executable;
    }

    void FormatBytes(const void* address, char* output, std::size_t outputSize)
    {
        if (!output || outputSize == 0)
            return;

        output[0] = '\0';
        constexpr std::size_t kByteCount = 16;
        if (!IsReadableRange(address, kByteCount))
        {
            strcpy_s(output, outputSize, "unreadable");
            return;
        }

        const auto* bytes =
            reinterpret_cast<const unsigned char*>(address);
        std::size_t used = 0;
        for (std::size_t i = 0; i < kByteCount && used < outputSize; ++i)
        {
            const int written = sprintf_s(
                output + used,
                outputSize - used,
                i == 0 ? "%02X" : " %02X",
                static_cast<unsigned int>(bytes[i]));
            if (written <= 0)
                break;
            used += static_cast<std::size_t>(written);
        }
    }

    bool TryMapRvaToRawOffset(
        const IMAGE_NT_HEADERS32* nt,
        std::size_t fileSize,
        std::uint32_t rva,
        std::size_t valueSize,
        std::size_t* rawOffset)
    {
        if (!nt || !rawOffset)
            return false;

        const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const IMAGE_SECTION_HEADER& section = sections[i];
            const std::uint32_t span = std::max(
                section.Misc.VirtualSize,
                section.SizeOfRawData);
            if (rva < section.VirtualAddress
                || rva >= section.VirtualAddress + span)
            {
                continue;
            }

            const std::size_t offset =
                static_cast<std::size_t>(section.PointerToRawData)
                + static_cast<std::size_t>(rva - section.VirtualAddress);
            if (offset > fileSize || valueSize > fileSize - offset)
                return false;

            *rawOffset = offset;
            return true;
        }
        return false;
    }

    CleanVTableTarget RecoverCleanVTableTarget(
        const ModuleImageSnapshot& module,
        void** runtimeVTable,
        std::size_t vtableIndex)
    {
        CleanVTableTarget result;
        if (!module.valid || !runtimeVTable)
            return result;

        if (!TryGetModuleRva(
                module,
                &runtimeVTable[vtableIndex],
                &result.vtableSlotRva))
        {
            return result;
        }

        HANDLE file = CreateFileA(
            module.path,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return result;

        LARGE_INTEGER fileSizeValue = {};
        if (!GetFileSizeEx(file, &fileSizeValue)
            || fileSizeValue.QuadPart <= 0)
        {
            CloseHandle(file);
            return result;
        }

        HANDLE mapping = CreateFileMappingA(
            file,
            nullptr,
            PAGE_READONLY,
            0,
            0,
            nullptr);
        if (!mapping)
        {
            CloseHandle(file);
            return result;
        }

        const auto* raw = reinterpret_cast<const unsigned char*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (!raw)
        {
            CloseHandle(mapping);
            CloseHandle(file);
            return result;
        }

        const std::size_t fileSize =
            static_cast<std::size_t>(fileSizeValue.QuadPart);
        bool parsed = false;
        if (fileSize >= sizeof(IMAGE_DOS_HEADER))
        {
            const auto* dos =
                reinterpret_cast<const IMAGE_DOS_HEADER*>(raw);
            const std::size_t ntOffset =
                static_cast<std::size_t>(dos->e_lfanew);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE
                && ntOffset <= fileSize
                && sizeof(IMAGE_NT_HEADERS32) <= fileSize - ntOffset)
            {
                const auto* nt =
                    reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw + ntOffset);
                const std::size_t sectionTableOffset =
                    ntOffset
                    + offsetof(IMAGE_NT_HEADERS32, OptionalHeader)
                    + nt->FileHeader.SizeOfOptionalHeader;
                const std::size_t sectionTableBytes =
                    static_cast<std::size_t>(nt->FileHeader.NumberOfSections)
                    * sizeof(IMAGE_SECTION_HEADER);
                if (nt->Signature == IMAGE_NT_SIGNATURE
                    && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
                    && sectionTableOffset <= fileSize
                    && sectionTableBytes <= fileSize - sectionTableOffset)
                {
                    std::size_t rawSlotOffset = 0;
                    if (TryMapRvaToRawOffset(
                            nt,
                            fileSize,
                            static_cast<std::uint32_t>(result.vtableSlotRva),
                            sizeof(result.preferredTarget),
                            &rawSlotOffset))
                    {
                        std::memcpy(
                            &result.preferredTarget,
                            raw + rawSlotOffset,
                            sizeof(result.preferredTarget));
                        const std::uintptr_t preferredImageBase =
                            nt->OptionalHeader.ImageBase;
                        if (result.preferredTarget >= preferredImageBase)
                        {
                            result.targetRva =
                                result.preferredTarget - preferredImageBase;
                        }
                        else
                        {
                            // Retain support for an RVA-form entry even though
                            // the x86 MSVC vtable normally stores a preferred VA.
                            result.targetRva = result.preferredTarget;
                        }
                        if (result.targetRva < nt->OptionalHeader.SizeOfImage)
                        {
                            result.target = reinterpret_cast<void*>(
                                module.base + result.targetRva);
                            result.resolved = true;
                        }
                    }
                    parsed = true;
                }
            }
        }

        UnmapViewOfFile(raw);
        CloseHandle(mapping);
        CloseHandle(file);
        if (!parsed)
            result = {};
        return result;
    }

    void LogVTableNeighborhood(void** vtable)
    {
        constexpr std::size_t kFirstIndex = 448u;
        constexpr std::size_t kLastIndex = 460u;
        if (!vtable
            || !IsReadableRange(
                &vtable[kFirstIndex],
                (kLastIndex - kFirstIndex + 1u) * sizeof(void*)))
        {
            return;
        }

        for (std::size_t index = kFirstIndex; index <= kLastIndex; ++index)
        {
            void* slotTarget = vtable[index];
            const MemoryRegionSnapshot region =
                QueryMemoryRegion(slotTarget);
            ModuleImageSnapshot owner;
            const bool hasOwner =
                TryGetModuleSnapshot(slotTarget, &owner);
            std::uintptr_t ownerRva = 0;
            const bool hasOwnerRva =
                hasOwner
                && TryGetModuleRva(owner, slotTarget, &ownerRva);
            U::LogInfo(
                "[PortalServerRunCommandHook][VTable] slot=%u target=%p executable=%s allocationBase=%p protect=0x%08X type=0x%08X owner=%p ownerRva=%s0x%08X ownerPath=%s mutation=false.\n",
                static_cast<unsigned int>(index),
                slotTarget,
                region.executable ? "true" : "false",
                region.allocationBase,
                static_cast<unsigned int>(region.protect),
                static_cast<unsigned int>(region.type),
                hasOwner ? owner.module : nullptr,
                hasOwnerRva ? "" : "invalid:",
                static_cast<unsigned int>(ownerRva),
                hasOwner && owner.path[0] ? owner.path : "none");
        }
    }

    const char* TimingName(PortalServerCommandTiming timing)
    {
        switch (timing)
        {
        case PortalServerCommandTiming::NoCommit: return "NoCommit";
        case PortalServerCommandTiming::BeforeCommit: return "BeforeCommit";
        case PortalServerCommandTiming::CommitCommand: return "CommitCommand";
        case PortalServerCommandTiming::AfterCommitWindow: return "AfterCommitWindow";
        case PortalServerCommandTiming::AfterWindow: return "AfterWindow";
        default: return "Unknown";
        }
    }

    const char* AngleSpaceName(PortalCommandAngleSpace angleSpace)
    {
        switch (angleSpace)
        {
        case PortalCommandAngleSpace::Unknown: return "Unknown";
        case PortalCommandAngleSpace::Entry: return "Entry";
        case PortalCommandAngleSpace::Exit: return "Exit";
        case PortalCommandAngleSpace::Ambiguous: return "Ambiguous";
        default: return "Unknown";
        }
    }

    bool ShouldLog(const PortalServerRunCommandObservation& observation)
    {
        return observation.hasCommittedTeleport
            && observation.commandDelta >= -1
            && observation.commandDelta <= 1;
    }

    bool AnglesEqual(const Vector& first, const Vector& second)
    {
        constexpr float kTolerance = 0.0001f;
        return std::fabs(first.x - second.x) <= kTolerance
            && std::fabs(first.y - second.y) <= kTolerance
            && std::fabs(first.z - second.z) <= kTolerance;
    }

    float Speed2D(const Vector& velocity)
    {
        return std::sqrt(
            velocity.x * velocity.x
            + velocity.y * velocity.y);
    }

    void RefreshServerFields(CBaseEntity* serverPlayer)
    {
        if (!serverPlayer || g_serverFields.entity == serverPlayer)
            return;

        g_serverFields = {};
        g_serverFields.entity = serverPlayer;
        datamap_t* dataMap =
            L4D2::ServerDataMap::GetDataDescMap(serverPlayer);
        if (!dataMap)
            return;

        g_serverFields.originResolved =
            L4D2::ServerDataMap::FindField(
                dataMap,
                "m_vecOrigin",
                &g_serverFields.origin)
            || L4D2::ServerDataMap::FindField(
                dataMap,
                "m_vecAbsOrigin",
                &g_serverFields.origin);
        g_serverFields.velocityResolved =
            L4D2::ServerDataMap::FindField(
                dataMap,
                "m_vecVelocity",
                &g_serverFields.velocity)
            || L4D2::ServerDataMap::FindField(
                dataMap,
                "m_vecAbsVelocity",
                &g_serverFields.velocity);
        g_serverFields.moveTypeResolved =
            L4D2::ServerDataMap::FindField(
                dataMap,
                "m_MoveType",
                &g_serverFields.moveType);
    }

    ServerStateSnapshot CaptureServerState(void* serverPlayer)
    {
        ServerStateSnapshot result;
        if (!serverPlayer
            || g_serverFields.entity != serverPlayer)
        {
            return result;
        }

        CBaseEntity* entity =
            reinterpret_cast<CBaseEntity*>(serverPlayer);
        result.originValid =
            g_serverFields.originResolved
            && L4D2::ServerDataMap::TryReadFieldVector(
                entity,
                g_serverFields.origin,
                &result.origin);
        result.velocityValid =
            g_serverFields.velocityResolved
            && L4D2::ServerDataMap::TryReadFieldVector(
                entity,
                g_serverFields.velocity,
                &result.velocity);
        result.moveTypeValid =
            g_serverFields.moveTypeResolved
            && L4D2::ServerDataMap::TryReadFieldInt(
                entity,
                g_serverFields.moveType,
                &result.moveType);
        return result;
    }
}

bool ServerPlayerRunCommand::EnsureInstalled(C_TerrorPlayer* clientPlayer)
{
    CBaseEntity* serverPlayer =
        PortalPlayerTeleport::ResolveServerLocalPlayer(clientPlayer);
    if (!serverPlayer)
        return false;

    g_localServerPlayer.store(serverPlayer, std::memory_order_release);
    RefreshServerFields(serverPlayer);
    if (g_hookInstalled)
        return true;

    if (g_lastAttemptedServerPlayer == serverPlayer)
        return false;
    g_lastAttemptedServerPlayer = serverPlayer;

    void*** objectVTable = reinterpret_cast<void***>(serverPlayer);
    if (!IsReadableRange(objectVTable, sizeof(void*))
        || !*objectVTable
        || !IsReadableRange(
            *objectVTable,
            (VTableIndex + 1u) * sizeof(void*)))
    {
        U::LogError("[PortalServerRunCommandHook] installed=false index=%u serverPlayer=%p validation=unreadable-vtable mutation=false.\n",
            static_cast<unsigned int>(VTableIndex),
            serverPlayer);
        return false;
    }

    void** vtable = *objectVTable;
    void* runtimeTarget = vtable[VTableIndex];
    const MemoryRegionSnapshot runtimeRegion =
        QueryMemoryRegion(runtimeTarget);
    ModuleImageSnapshot runtimeTargetOwner;
    const bool runtimeTargetHasOwner =
        TryGetModuleSnapshot(runtimeTarget, &runtimeTargetOwner);
    ModuleImageSnapshot vtableOwner;
    const bool vtableHasOwner =
        TryGetModuleSnapshot(vtable, &vtableOwner);
    std::uintptr_t runtimeTargetRva = 0;
    const bool runtimeTargetInVTableModule =
        vtableHasOwner
        && IsExecutableModuleAddress(
            vtableOwner,
            runtimeTarget,
            &runtimeTargetRva);
    char runtimeBytes[64] = {};
    FormatBytes(runtimeTarget, runtimeBytes, sizeof(runtimeBytes));

    U::LogInfo(
        "[PortalServerRunCommandHook][TargetProbe] index=%u serverPlayer=%p vtable=%p vtableOwner=%p vtableOwnerPath=%s runtimeTarget=%p queried=%s state=0x%08X protect=0x%08X type=0x%08X allocationBase=%p executable=%s targetOwner=%p targetOwnerPath=%s inVTableModule=%s rva=%s0x%08X bytes=%s mutation=false.\n",
        static_cast<unsigned int>(VTableIndex),
        serverPlayer,
        vtable,
        vtableHasOwner ? vtableOwner.module : nullptr,
        vtableHasOwner && vtableOwner.path[0]
            ? vtableOwner.path
            : "none",
        runtimeTarget,
        runtimeRegion.queried ? "true" : "false",
        static_cast<unsigned int>(runtimeRegion.state),
        static_cast<unsigned int>(runtimeRegion.protect),
        static_cast<unsigned int>(runtimeRegion.type),
        runtimeRegion.allocationBase,
        runtimeRegion.executable ? "true" : "false",
        runtimeTargetHasOwner ? runtimeTargetOwner.module : nullptr,
        runtimeTargetHasOwner && runtimeTargetOwner.path[0]
            ? runtimeTargetOwner.path
            : "none",
        runtimeTargetInVTableModule ? "true" : "false",
        runtimeTargetInVTableModule ? "" : "invalid:",
        static_cast<unsigned int>(runtimeTargetRva),
        runtimeBytes);
    LogVTableNeighborhood(vtable);

    const CleanVTableTarget clean =
        vtableHasOwner
        ? RecoverCleanVTableTarget(
            vtableOwner,
            vtable,
            VTableIndex)
        : CleanVTableTarget();
    std::uintptr_t cleanValidatedRva = 0;
    const bool cleanExecutable =
        clean.resolved
        && IsExecutableModuleAddress(
            vtableOwner,
            clean.target,
            &cleanValidatedRva);
    char cleanBytes[64] = {};
    FormatBytes(clean.target, cleanBytes, sizeof(cleanBytes));
    U::LogInfo(
        "[PortalServerRunCommandHook][DiskRecovery] index=%u module=%p path=%s resolved=%s slotRva=0x%08X preferredTarget=0x%08X target=%p targetRva=0x%08X validatedRva=0x%08X executable=%s bytes=%s mutation=false.\n",
        static_cast<unsigned int>(VTableIndex),
        vtableHasOwner ? vtableOwner.module : nullptr,
        vtableHasOwner && vtableOwner.path[0]
            ? vtableOwner.path
            : "none",
        clean.resolved ? "true" : "false",
        static_cast<unsigned int>(clean.vtableSlotRva),
        static_cast<unsigned int>(clean.preferredTarget),
        clean.target,
        static_cast<unsigned int>(clean.targetRva),
        static_cast<unsigned int>(cleanValidatedRva),
        cleanExecutable ? "true" : "false",
        cleanBytes);

    // SourceHook may have replaced the runtime vtable slot with a generated
    // executable thunk outside server.dll. Hook the clean function body from
    // the on-disk image in that case, so SourceHook's dispatch chain remains
    // intact and eventually reaches this detour through its saved original.
    void* hookTarget = nullptr;
    const char* resolution = "none";
    std::uintptr_t hookTargetRva = 0;
    if (runtimeTargetInVTableModule)
    {
        hookTarget = runtimeTarget;
        hookTargetRva = runtimeTargetRva;
        resolution = "runtime-vtable";
    }
    else if (cleanExecutable)
    {
        hookTarget = clean.target;
        hookTargetRva = cleanValidatedRva;
        resolution = "disk-clean-vtable";
    }

    if (!hookTarget)
    {
        U::LogError("[PortalServerRunCommandHook] installed=false index=%u serverPlayer=%p vtable=%p runtimeTarget=%p runtimeExecutable=%s runtimeInServer=%s cleanResolved=%s cleanExecutable=%s resolution=none mutation=false.\n",
            static_cast<unsigned int>(VTableIndex),
            serverPlayer,
            vtable,
            runtimeTarget,
            runtimeRegion.executable ? "true" : "false",
            runtimeTargetInVTableModule ? "true" : "false",
            clean.resolved ? "true" : "false",
            cleanExecutable ? "true" : "false");
        return false;
    }

    const bool created = PlayerRunCommand::Func.Init(
        hookTarget,
        reinterpret_cast<void*>(&PlayerRunCommand::Detour));
    const MH_STATUS enableStatus =
        created ? MH_EnableHook(hookTarget) : MH_ERROR_NOT_CREATED;
    g_hookInstalled =
        created && enableStatus == MH_STATUS::MH_OK;
    if (g_hookInstalled)
        g_hookTarget = hookTarget;

    U::LogInfo("[PortalServerRunCommandHook] installed=%s index=%u serverPlayer=%p vtable=%p runtimeTarget=%p hookTarget=%p hookTargetRva=0x%08X resolution=%s runtimeExecutable=%s cleanExecutable=%s created=%s enableStatus=%d fields(origin=%s@0x%X velocity=%s@0x%X moveType=%s@0x%X) mutation=false.\n",
        g_hookInstalled ? "true" : "false",
        static_cast<unsigned int>(VTableIndex),
        serverPlayer,
        vtable,
        runtimeTarget,
        hookTarget,
        static_cast<unsigned int>(hookTargetRva),
        resolution,
        runtimeRegion.executable ? "true" : "false",
        cleanExecutable ? "true" : "false",
        created ? "true" : "false",
        static_cast<int>(enableStatus),
        g_serverFields.originResolved ? "true" : "false",
        g_serverFields.origin.actualOffset,
        g_serverFields.velocityResolved ? "true" : "false",
        g_serverFields.velocity.actualOffset,
        g_serverFields.moveTypeResolved ? "true" : "false",
        g_serverFields.moveType.actualOffset);
    return g_hookInstalled;
}

bool ServerPlayerRunCommand::IsInstalled()
{
    return g_hookInstalled && g_hookTarget != nullptr;
}

CUserCmd* ServerPlayerRunCommand::GetActiveCommand()
{
    return g_activeServerCommand;
}

void __fastcall ServerPlayerRunCommand::PlayerRunCommand::Detour(
    void* ecx,
    void* edx,
    CUserCmd* command,
    IMoveHelper* moveHelper)
{
    const bool isLocalServerPlayer =
        ecx == g_localServerPlayer.load(std::memory_order_acquire);
    PortalServerRunCommandObservation observation;
    CPortalTransitionSimulator& simulator =
        G::G_L4D2Portal.m_PortalTransitionSimulator;
    const PortalTransitionContext context =
        simulator.GetContext();
    const bool observed =
        isLocalServerPlayer
        && command
        && simulator.ObserveServerRunCommand(command, &observation);
    const bool shouldLog =
        observed && ShouldLog(observation);
    const Vector anglesBefore =
        command ? command->viewangles : Vector();
    const float forwardMoveBefore =
        command ? command->forwardmove : 0.0f;
    const float sideMoveBefore =
        command ? command->sidemove : 0.0f;
    const float upMoveBefore =
        command ? command->upmove : 0.0f;
    const int buttonsBefore =
        command ? command->buttons : 0;
    const std::uint32_t callSequence =
        isLocalServerPlayer ? ++g_localCallSequence : 0;
    const ServerStateSnapshot serverStateBefore =
        shouldLog ? CaptureServerState(ecx) : ServerStateSnapshot();
    const bool angleFixEnabled =
        PortalTransitionDecision::GetServerPortalCommandAngleFixEnabled();
    const bool shouldApplyAngleFix =
        observed
        && observation.commandAngleFixPending
        && observation.hasEntryToExitMatrix
        && PortalTransitionDecision::ShouldApplyServerPortalCommandAngleFix(
            angleFixEnabled,
            observation.timing,
            observation.angleSpace);

    if (shouldLog)
    {
        U::LogInfo("[PortalServerRunCommand] stage=pre call=%u serverPlayer=%p txn=%u epoch=%u phase=%d ctx(lastCmd=%d teleportCmd=%d) cmd=%d committedCmd=%d cmdDelta=%d timing=%s angleSpace=%s angleDiff(pre=%.3f post=%.3f) cmdAngles=(%.3f %.3f %.3f) portalAngles(pre=(%.3f %.3f %.3f) post=(%.3f %.3f %.3f)) input(forward=%.3f side=%.3f up=%.3f buttons=0x%X) serverState(valid(origin=%s velocity=%s moveType=%s) origin=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f) speed2D=%.3f speed3D=%.3f moveType=%d) serverCommandAngleFix(enabled=%s pending=%s matrix=%s decision=%s) movementMutation=false.\n",
            callSequence,
            ecx,
            observation.transactionId,
            context.traversalEpoch,
            static_cast<int>(context.phase),
            context.lastCommandNumber,
            context.teleportCommandNumber,
            observation.commandNumber,
            observation.committedCommandNumber,
            observation.commandDelta,
            TimingName(observation.timing),
            AngleSpaceName(observation.angleSpace),
            observation.differenceFromPrePortalAngles,
            observation.differenceFromPostPortalAngles,
            anglesBefore.x,
            anglesBefore.y,
            anglesBefore.z,
            observation.prePortalAngles.x,
            observation.prePortalAngles.y,
            observation.prePortalAngles.z,
            observation.postPortalAngles.x,
            observation.postPortalAngles.y,
            observation.postPortalAngles.z,
            forwardMoveBefore,
            sideMoveBefore,
            upMoveBefore,
            buttonsBefore,
            serverStateBefore.originValid ? "true" : "false",
            serverStateBefore.velocityValid ? "true" : "false",
            serverStateBefore.moveTypeValid ? "true" : "false",
            serverStateBefore.origin.x,
            serverStateBefore.origin.y,
            serverStateBefore.origin.z,
            serverStateBefore.velocity.x,
            serverStateBefore.velocity.y,
            serverStateBefore.velocity.z,
            Speed2D(serverStateBefore.velocity),
            serverStateBefore.velocity.Lenght(),
            serverStateBefore.moveType,
            angleFixEnabled ? "true" : "false",
            observation.commandAngleFixPending ? "true" : "false",
            observation.hasEntryToExitMatrix ? "true" : "false",
            shouldApplyAngleFix ? "true" : "false");
    }

    if (shouldApplyAngleFix && command)
    {
        QAngle sourceAngles;
        sourceAngles.x = command->viewangles.x;
        sourceAngles.y = command->viewangles.y;
        sourceAngles.z = command->viewangles.z;
        const QAngle transformedAngles =
            PortalTransform::TransformAngles(
                observation.entryToExit,
                sourceAngles);
        command->viewangles.x = transformedAngles.x;
        command->viewangles.y = transformedAngles.y;
        command->viewangles.z = transformedAngles.z;
    }

    const bool angleFixConsumed =
        observed
        && observation.commandAngleFixPending
        && simulator.ConsumeServerPortalCommandAngleFix(
            observation.commandNumber);
    const Vector anglesBeforeOriginal =
        command ? command->viewangles : Vector();

    if (shouldLog
        && (observation.commandAngleFixPending
            || shouldApplyAngleFix
            || angleFixConsumed))
    {
        U::LogInfo("[PortalServerRunCommandFix] call=%u txn=%u cmd=%d committedCmd=%d timing=%s angleSpace=%s enabled=%s pending=%s matrix=%s decision=%s applied=%s consumed=%s angles=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) input(forward=%.3f side=%.3f up=%.3f buttons=0x%X) movementMutation=false commandAngleMutation=%s.\n",
            callSequence,
            observation.transactionId,
            observation.commandNumber,
            observation.committedCommandNumber,
            TimingName(observation.timing),
            AngleSpaceName(observation.angleSpace),
            angleFixEnabled ? "true" : "false",
            observation.commandAngleFixPending ? "true" : "false",
            observation.hasEntryToExitMatrix ? "true" : "false",
            shouldApplyAngleFix ? "true" : "false",
            shouldApplyAngleFix ? "true" : "false",
            angleFixConsumed ? "true" : "false",
            anglesBefore.x,
            anglesBefore.y,
            anglesBefore.z,
            anglesBeforeOriginal.x,
            anglesBeforeOriginal.y,
            anglesBeforeOriginal.z,
            forwardMoveBefore,
            sideMoveBefore,
            upMoveBefore,
            buttonsBefore,
            shouldApplyAngleFix ? "true" : "false");
    }

    CUserCmd* previousActiveCommand = g_activeServerCommand;
    g_activeServerCommand = isLocalServerPlayer ? command : nullptr;
    Func.Original<FN>()(ecx, edx, command, moveHelper);
    g_activeServerCommand = previousActiveCommand;

    if (shouldLog && command)
    {
        const ServerStateSnapshot serverStateAfter =
            CaptureServerState(ecx);
        const Vector velocityDelta =
            serverStateAfter.velocity - serverStateBefore.velocity;
        const bool changedByOriginal =
            !AnglesEqual(anglesBeforeOriginal, command->viewangles)
            || std::fabs(forwardMoveBefore - command->forwardmove) > 0.0001f
            || std::fabs(sideMoveBefore - command->sidemove) > 0.0001f
            || std::fabs(upMoveBefore - command->upmove) > 0.0001f
            || buttonsBefore != command->buttons;
        U::LogInfo("[PortalServerRunCommand] stage=post call=%u txn=%u cmd=%d cmdAngles=(%.3f %.3f %.3f) input(forward=%.3f side=%.3f up=%.3f buttons=0x%X) serverState(valid(origin=%s velocity=%s moveType=%s) origin=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f) velocityDelta=(%.3f %.3f %.3f) speed2D=%.3f->%.3f speed3D=%.3f->%.3f moveType=%d->%d) angleFixApplied=%s changedByOriginal=%s movementMutation=false.\n",
            callSequence,
            observation.transactionId,
            command->command_number,
            command->viewangles.x,
            command->viewangles.y,
            command->viewangles.z,
            command->forwardmove,
            command->sidemove,
            command->upmove,
            command->buttons,
            serverStateAfter.originValid ? "true" : "false",
            serverStateAfter.velocityValid ? "true" : "false",
            serverStateAfter.moveTypeValid ? "true" : "false",
            serverStateAfter.origin.x,
            serverStateAfter.origin.y,
            serverStateAfter.origin.z,
            serverStateAfter.velocity.x,
            serverStateAfter.velocity.y,
            serverStateAfter.velocity.z,
            velocityDelta.x,
            velocityDelta.y,
            velocityDelta.z,
            Speed2D(serverStateBefore.velocity),
            Speed2D(serverStateAfter.velocity),
            serverStateBefore.velocity.Lenght(),
            serverStateAfter.velocity.Lenght(),
            serverStateBefore.moveType,
            serverStateAfter.moveType,
            shouldApplyAngleFix ? "true" : "false",
            changedByOriginal ? "true" : "false");
    }
}
