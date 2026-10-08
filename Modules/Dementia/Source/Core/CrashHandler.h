#pragma once
#include <string>

namespace CrashHandler
{
    // Installs process-wide handlers for fatal signals (POSIX), unhandled SEH exceptions (Win64)
    // and std::terminate. On a crash a report with the call stack is written to
    // <crashDirectory>/crash_<timestamp>.txt (plus a .dmp minidump on Win64) and the log is flushed.
    void Install( const std::string& crashDirectory );

    // Path of the report a crash in this session would be written to.
    const std::string& GetReportPath();
}
