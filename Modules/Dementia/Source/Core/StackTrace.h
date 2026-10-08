#pragma once
#include <string>

namespace StackTrace
{
    // Human readable call stack of the calling thread, one frame per line.
    // skipFrames drops the innermost frames (CaptureString itself is always skipped).
    std::string CaptureString( int skipFrames = 0, int maxFrames = 64 );
}

namespace Debugger
{
    // True when a native debugger (gdb, lldb, Visual Studio) is attached to this process.
    bool IsAttached();
}
