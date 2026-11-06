#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

class NetReplayReader;

// ReplayDump (ME-316): a match replay as text or JSON, headless, for people, CI and Claude. Any
// game executable runs it before the engine starts (ME_APPLICATION_MAIN), so it needs no project,
// window or scripts:
//
//   Player --replay-dump file.mreplay [--ticks a:b] [--entity id] [--events] [--shots] [--commands]
//          [--net] [--states] [--summary] [--json]
//
// Sections: --summary (the header, chunks, entities, clients and counts), --events, --shots (with
// the rewound hitboxes), --commands (every tick's, per client), --net (owed / dropped ticks and each
// client's link, per tick), --states (each entity's pose and field bytes, per tick). With none of
// them: the summary, events, shots, and each client's command whenever its input changes or it
// missed one. --ticks limits to a range (a:b, a: or :b), --entity to one NetId (its events, the
// shots it fired or took, its owner's commands, its states). --json prints one document with the
// same sections.
namespace NetReplayDump
{
    struct Options
    {
        std::string Path;
        uint32_t From = 0;
        uint32_t To = 0xFFFFFFFFu;
        uint32_t Entity = 0;    // 0: every entity
        bool Summary = false;
        bool Events = false;
        bool Shots = false;
        bool Commands = false;  // every tick's
        bool CommandChanges = false;
        bool Net = false;
        bool States = false;
        bool Json = false;
    };

    // InArgs: the whole command line (argv). False with OutError on a bad option.
    bool ParseArgs( const std::vector<std::string>& InArgs, Options& OutOptions, std::string& OutError );
    // The dump of InReader to InOut; false with OutError when the replay won't read.
    bool Write( NetReplayReader& InReader, const Options& InOptions, std::ostream& InOut, std::string& OutError );

    // --replay-dump is on the command line.
    bool IsRequested();
    // Reads the replay the command line names and prints it: the exit code (0, 1 when it can't be
    // read, 2 for a bad option).
    int Run();
}
