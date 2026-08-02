#include "../src/Util/Logger/PortalFileLog.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }

    std::string ReadLog()
    {
        std::ifstream input(U::PortalFileLog::kPath, std::ios::binary);
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }
}

int main()
{
    std::remove(U::PortalFileLog::kPath);

    U::PortalFileLog::Reset();
    Expect(U::PortalFileLog::IsSessionOpen(),
        "reset must open one persistent focused-log session");
    Expect(std::string(U::PortalFileLog::GetBuildId()).find("pe-") == 0,
        "the build identity must come from the linked module, not a stale translation-unit timestamp");
    Expect(U::PortalFileLog::GetSessionOpenCountForDiagnostics() == 1,
        "the focused-log session must be opened exactly once");

    U::PortalFileLog::Write("[PortalBsp][RuntimeTest] routed=true.");
    U::PortalFileLog::Write("[PortalBsp][RuntimeTest] routed=second.");
    U::PortalFileLog::WriteAlways("[PortalPredictionCommandWindowTest] direct=true.");

    Expect(U::PortalFileLog::GetSessionOpenCountForDiagnostics() == 1,
        "multiple writes must reuse the persistent file session");

    U::PortalFileLog::Flush();
    const std::string log = ReadLog();
    Expect(log.find("[PortalFileLog] build id=") != std::string::npos,
        "the session header must identify the loaded build");
    Expect(log.find("modulePath=") != std::string::npos,
        "the session header must identify the loaded module path");
    Expect(log.find("ioMode=persistent-buffered") != std::string::npos,
        "the session header must declare the low-overhead I/O mode");
    Expect(log.find("routed=second") != std::string::npos,
        "routed writes must reach the focused log after an explicit flush");
    Expect(log.find("PortalPredictionCommandWindowTest") != std::string::npos,
        "direct diagnostics must bypass the focused-log routing policy");

    U::PortalFileLog::Shutdown();
    Expect(!U::PortalFileLog::IsSessionOpen(),
        "shutdown must flush and close the persistent session");

    std::remove(U::PortalFileLog::kPath);
    std::puts("PortalFileLog runtime tests passed");
    return 0;
}
