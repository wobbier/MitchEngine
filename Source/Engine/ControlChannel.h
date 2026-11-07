#pragma once
#include <string>

// A console over stdin (NET-7: the editor's PIE child clients), for a process another one drives:
// with --control-stdin, each line arriving on stdin is "<id> <console line>" and runs through the
// DevConsole (DevConsole::Execute: every C++ and C# command, "net field", "net timeline" ...);
// the answer goes to stdout as one line, "[control] <id> <the output as a JSON string>", which the
// driver matches by id. A line without an id runs the same, answered with id 0. When stdin closes
// (the driver stopped it, or died: its end of the pipe went with it) the process quits through
// the engine's quit path, so a child never outlives the editor that started it.
//
// Polled on the main thread once a frame (Engine::Run, before the frame simulates): reads what has
// arrived without blocking and runs whole lines. POSIX only; elsewhere --control-stdin is refused
// with a warning.
class ControlChannel
{
public:
    // Reads --control-stdin: true when the channel is on.
    bool Init();
    bool IsActive() const { return m_active; }
    // Runs the lines that arrived. True when stdin closed (the caller quits).
    bool Poll();

    // The answer line for an id and a command's output (tests).
    static std::string FormatReply( unsigned long long InId, const std::string& InOutput );

private:
    void RunLine( const std::string& InLine );

    bool m_active = false;
    bool m_closed = false;
    std::string m_pending;
};
