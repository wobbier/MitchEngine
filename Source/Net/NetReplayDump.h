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
//          [--net] [--states] [--markers] [--summary] [--json]
//
// Sections: --summary (the header, chunks, entities, clients and counts), --events, --shots (with
// the rewound hitboxes), --commands (every tick's, per client), --net (owed / dropped ticks and each
// client's link, per tick), --states (each entity's pose and field bytes, and each character's mover
// at full precision with its [Predicted] bytes, per tick), --markers (the viewer's timeline markers:
// kills, joins, leaves, events, hits, misses, resyncs, hitches; NetReplayTimeline.h). With none of
// them: the summary, events, shots, and each client's command whenever its input changes or it
// missed one. --ticks limits to a range (a:b, a: or :b), --entity to one NetId (its events, the
// shots it fired or took, its owner's commands, its states). --json prints one document with the
// same sections.
//
// Heatmaps (ME-215, NetReplayHeatmap.h): one replay or many of the same scene into a heatmap of
// where players were, died and killed, instead of the dump:
//
//   Player --replay-dump a.mreplay [b.mreplay | folder ...] --heatmap OUT [--heatmap-cell M]
//          [--heatmap-sample-ticks N] [--heatmap-groups]
//
// More replays follow the first straight after it (a folder: every .mreplay in it, by name). OUT
// ending in .json is the heatmap's JSON, its PNGs beside it as <stem>.<layer>.png and the overview
// <stem>.png; any other OUT is a folder holding heatmap.json and heatmap.*.png. --heatmap-groups
// also writes an overview per hero and team (<stem>.hero-Striker.png). A replay that won't read, or
// is of another scene than the first, is skipped with a warning; none read is exit code 1.
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
        bool Markers = false;   // the timeline's (NetReplayTimeline)
        bool Json = false;
        // --heatmap (ME-215): the replays after the first, and where the heatmap goes.
        std::vector<std::string> MorePaths;
        std::string Heatmap;
        float HeatmapCell = 1.f;
        uint32_t HeatmapSampleTicks = 6;
        bool HeatmapGroups = false;
    };

    // InArgs: the whole command line (argv). False with OutError on a bad option.
    bool ParseArgs( const std::vector<std::string>& InArgs, Options& OutOptions, std::string& OutError );
    // The dump of InReader to InOut; false with OutError when the replay won't read.
    bool Write( NetReplayReader& InReader, const Options& InOptions, std::ostream& InOut, std::string& OutError );

    // --heatmap: every replay the options name (folders expanded) into one heatmap, written where
    // they say, its summary and the files written to InOut. False with OutError when no replay read
    // or a file couldn't be written; a replay skipped is a warning line in InOut.
    bool WriteHeatmap( const Options& InOptions, std::ostream& InOut, std::string& OutError );
    // The replays the options name: the first, then the rest, a folder as its .mreplay files by name.
    std::vector<std::string> ExpandPaths( const Options& InOptions );

    // --replay-dump is on the command line.
    bool IsRequested();
    // Reads the replay the command line names and prints it: the exit code (0, 1 when it can't be
    // read, 2 for a bad option).
    int Run();
}
