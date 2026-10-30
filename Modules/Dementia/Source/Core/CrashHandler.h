#pragma once
#include <string>
#include <vector>

namespace CrashHandler
{
    // Installs process-wide handlers for fatal signals (POSIX), unhandled SEH exceptions (Win64)
    // and std::terminate. On a crash a report with the call stack is written to
    // <crashDirectory>/crash_<timestamp>.txt (plus a .dmp minidump on Win64) and the log is flushed.
    void Install( const std::string& crashDirectory );
    // Moves where reports go without touching the handlers. Once .NET is up it has chained its
    // own signal handlers over these, and installing again would take them away.
    void SetDirectory( const std::string& crashDirectory );

    // Path of the report a crash in this session would be written to.
    const std::string& GetReportPath();

    // PLAY-9: reports in the current directory that an earlier session wrote and nobody has
    // acknowledged yet (oldest first). The engine shows them at start-up, then acknowledges them
    // (<dir>/Acknowledged.txt names the newest one seen).
    std::vector<std::string> FindNewReports();
    void AcknowledgeReports();

    // A deliberate crash through the real fault path (--crash-test).
    [[noreturn]] void CrashForTest();
}
