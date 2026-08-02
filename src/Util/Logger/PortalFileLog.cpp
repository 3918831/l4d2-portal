#include "PortalFileLog.h"

#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <share.h>

namespace
{
    constexpr std::size_t kFileBufferBytes = 8u * 1024u * 1024u;

    std::mutex g_sessionMutex;
    FILE* g_file = nullptr;
    alignas(64) char g_fileBuffer[kFileBufferBytes];
    char g_buildId[32] = "pe-unresolved";
    char g_loadedModulePath[MAX_PATH] = "unresolved";
    void* g_loadedModuleBase = nullptr;
    unsigned int g_sessionOpenCount = 0;

    void ResolveLoadedModuleIdentity()
    {
        HMODULE module = nullptr;
        const auto address = reinterpret_cast<LPCSTR>(
            reinterpret_cast<std::uintptr_t>(&ResolveLoadedModuleIdentity));
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                address,
                &module)
            || !module)
        {
            return;
        }

        g_loadedModuleBase = module;
        const auto* dosHeader =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE)
        {
            const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                reinterpret_cast<const unsigned char*>(module)
                + dosHeader->e_lfanew);
            if (ntHeaders->Signature == IMAGE_NT_SIGNATURE)
            {
                sprintf_s(
                    g_buildId,
                    "pe-%08X",
                    static_cast<unsigned int>(
                        ntHeaders->FileHeader.TimeDateStamp));
            }
        }

        const DWORD length = GetModuleFileNameA(
            module,
            g_loadedModulePath,
            static_cast<DWORD>(sizeof(g_loadedModulePath)));
        if (length == 0 || length >= sizeof(g_loadedModulePath))
            strcpy_s(g_loadedModulePath, "unresolved");
    }

    bool OpenSessionLocked()
    {
        if (g_file)
            return true;

        // Keep one buffered writer for the whole session, but do not deny
        // readers: testers must be able to copy the flushed log while the
        // injected module (and therefore this FILE*) is still alive.
        g_file = _fsopen(U::PortalFileLog::kPath, "wb", _SH_DENYNO);
        if (!g_file)
            return false;

        ++g_sessionOpenCount;
        setvbuf(g_file, g_fileBuffer, _IOFBF, sizeof(g_fileBuffer));
        ResolveLoadedModuleIdentity();

        std::fprintf(
            g_file,
            "[PortalFileLog] reset path=%s\n",
            U::PortalFileLog::kPath);
        std::fprintf(
            g_file,
            "[PortalFileLog] capture gmod=%s legacyBridge=%s legacyStageProbe=%s legacySimulator=%s render=%s\n",
            U::PortalFileLog::kCaptureGModTraversal ? "on" : "off",
            U::PortalFileLog::kCaptureLegacyBridge ? "on" : "off",
            U::PortalFileLog::kCaptureLegacyStageProbe ? "on" : "off",
            U::PortalFileLog::kCaptureLegacySimulator ? "on" : "off",
            U::PortalFileLog::kCaptureRenderDiagnostics ? "on" : "off");
        std::fprintf(
            g_file,
            "[PortalFileLog] build id=%s moduleBase=%p modulePath=%s "
            "ioMode=persistent-buffered bufferBytes=%zu flush=explicit.\n",
            g_buildId,
            g_loadedModuleBase,
            g_loadedModulePath,
            sizeof(g_fileBuffer));

        // Make the build identity immediately visible. Hot-path records after
        // initialization remain in the large user-space buffer until an
        // explicit status/flush command or shutdown.
        std::fflush(g_file);
        return true;
    }

    void WriteLocked(const char* text)
    {
        if (!text || !OpenSessionLocked())
            return;

        std::fputs(text, g_file);
        const std::size_t length = std::strlen(text);
        if (length == 0 || text[length - 1] != '\n')
            std::fputc('\n', g_file);
    }

    void WriteFormatted(bool bypassRouting, const char* format, va_list args)
    {
        if (!format)
            return;

        char buffer[2048];
        vsprintf_s(buffer, format, args);
        if (bypassRouting)
            U::PortalFileLog::WriteAlways(buffer);
        else
            U::PortalFileLog::Write(buffer);
    }
}

void U::PortalFileLog::Reset()
{
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!OpenSessionLocked())
        return;

    std::fprintf(g_file, "[PortalFileLog] portal init marker path=%s\n", kPath);
    std::fflush(g_file);
}

void U::PortalFileLog::Write(const char* text)
{
    if (!ShouldCapture(text))
        return;

    std::lock_guard<std::mutex> lock(g_sessionMutex);
    WriteLocked(text);
}

void U::PortalFileLog::WriteFormat(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    WriteFormatted(false, format, args);
    va_end(args);
}

void U::PortalFileLog::WriteAlways(const char* text)
{
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    WriteLocked(text);
}

void U::PortalFileLog::WriteAlwaysFormat(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    WriteFormatted(true, format, args);
    va_end(args);
}

void U::PortalFileLog::Flush()
{
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (g_file)
        std::fflush(g_file);
}

void U::PortalFileLog::Shutdown()
{
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_file)
        return;

    std::fflush(g_file);
    std::fclose(g_file);
    g_file = nullptr;
}

const char* U::PortalFileLog::GetBuildId()
{
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    OpenSessionLocked();
    return g_buildId;
}

const char* U::PortalFileLog::GetLoadedModulePath()
{
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    OpenSessionLocked();
    return g_loadedModulePath;
}

bool U::PortalFileLog::IsSessionOpen()
{
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    return g_file != nullptr;
}

unsigned int U::PortalFileLog::GetSessionOpenCountForDiagnostics()
{
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    return g_sessionOpenCount;
}
