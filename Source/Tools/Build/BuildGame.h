#pragma once
#include "Engine/ProjectSettings.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Build Game (SHIP-3, Engine/Docs/Building.md): one shippable build of the project from a build
// profile (Project Settings > Build), for the machine building it. The steps:
//   1. Compile - the game executable in the profile's configuration (Retail by default);
//   2. Scripts - the C# script project in Release;
//   3. Cook    - the profile's scenes (the SHIP-2 cooker), as one Game.mpak or a loose tree;
//   4. Runtime - an app-local .NET (dotnet/), copied from the build machine's install;
//   5. Package - the libraries the executable needs, the launcher, the icon, BuildInfo.json and
//                an optional archive.
// The output runs from any directory on a machine without .NET or the engine's sources.
namespace BuildGame
{
    enum class Step
    {
        Compile,
        Scripts,
        Cook,
        Runtime,
        Package,
        Count
    };
    const char* StepName( Step InStep );

    struct Request
    {
        ProjectSettings::BuildProfile Profile;
        // False: package the configuration's existing executable (--no-compile).
        bool Compile = true;
        // Records each scene through a tools game build (--record-deps), so assets only scripts
        // load by name ship too; false: a static cook only.
        bool Record = true;
        // The tools game build to record with. Empty: the one matching this editor build
        // (RecordingConfiguration), compiled along with the game when Compile is set, otherwise
        // the newest FindRecordingExecutable finds.
        std::string RecordExecutable;
        // Each line the build's commands print (compiler, dotnet, tar), from the building thread.
        // The builder's own messages go to the log; .tmp/BuildGame/Staging/<Profile>/Build.log has both.
        std::function<void( const std::string& )> OnLine;
        // Called as each step starts, from the building thread.
        std::function<void( Step )> OnStep;
        // Checked between steps; set it to stop the build after the running one.
        const std::atomic<bool>* Cancel = nullptr;
    };

    struct Report
    {
        bool Success = false;
        std::string OutputDirectory;
        // What players start: the launcher on Linux, the executable elsewhere.
        std::string Launcher;
        uint64_t ByteCount = 0;
        double Seconds = 0.0;
        std::vector<std::string> Errors;
        std::vector<std::string> Warnings;
    };

    // Where a profile builds to: its OutputDirectory, or Builds/<Name>.
    std::string OutputDirectory( const ProjectSettings::BuildProfile& InProfile );
    // The executable a configuration's game build produces, e.g.
    // ".build/Game_linux_Retail/Game_EntryPoint_x64".
    std::string GameExecutable( const std::string& InConfiguration );
    // The tools game configuration matching this editor build: "Release" next to a Release
    // editor, else "Debug".
    std::string RecordingConfiguration();
    // The newest tools game build (Debug or Release) next to this editor build, e.g.
    // ".build/Game_linux_Debug/Game_EntryPoint_x64", which the cooker records scenes through;
    // empty when there is none.
    std::string FindRecordingExecutable();
    // The file name a product's executable ships as ("My Game" -> "My Game.x86_64" on Linux,
    // "My Game.exe" on Windows).
    std::string ShippedExecutableName( const std::string& InProductName );

    // Tools builds; runs on the calling thread.
    Report Run( const Request& InRequest );
}
