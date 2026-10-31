#pragma once
#include <string>

// The project this process runs and the engine it uses (SHIP-4, Engine/Docs/Projects.md).
//
// A project is a folder: Assets/ plus Project/Game.meproj. Every executable changes into its
// project before anything resolves a Path (ME_APPLICATION_MAIN, Havana's main), so the working
// directory and the VFS's game root are the project. --project <folder | .meproj> picks it;
// otherwise it is the working directory. Its engine is:
//   1. <project>/Engine when that is an engine (the submodule layout every C++ game uses);
//   2. otherwise, for a folder with a .meproj, the running executable's own engine: the nearest
//      Engine/ at or above the executable's folder (a dev build in <checkout>/.build/<config>/,
//      or an install that ships Engine/ beside its binaries). The VFS then serves "Engine/..."
//      from there, so a C#-only project needs no copy of the engine.
// A folder without a .meproj (a cooked tree, a shipped build) never borrows engine files from
// wherever its executable happens to sit. An engine checkout's own root is its own engine.
class Project
{
public:
    static constexpr const char* kProjectFile = "Project/Game.meproj";

    // Applies --project, changes into the project and points the VFS at it and its engine. False
    // when --project doesn't name a project (reported on stderr: the log isn't up yet).
    static bool InitFromCommandLine();

    // Absolute, '/'-separated, no trailing slash.
    static std::string GetRoot();
    static std::string GetEngineRoot();
    // The engine is the project's Engine/ subfolder (as opposed to the executable's engine).
    static bool HasEmbeddedEngine();
    // The .meproj's "ProjectName", else the project folder's name.
    static std::string GetName();
    // The project has C++ game code (Game.sharpmake.cs), so it needs its own editor and game builds.
    static bool HasGameModule();
    // The running executable was built inside this project (<root>/.build/...), rather than being
    // a shared editor or Player from another checkout or an install.
    static bool IsExecutableInProject();

    // The running executable's folder: '/'-separated, with a trailing slash.
    static std::string GetExecutableDirectory();

    // The project root InFolderOrFile names: a folder holding Project/Game.meproj, or a .meproj
    // file in such a folder's Project/. Empty when it names no project.
    static std::string FindProjectRoot( const std::string& InFolderOrFile );
    // An engine folder (holds the engine's own Assets/Config/HardwareProfiles.json).
    static bool IsEngineDirectory( const std::string& InDirectory );
    // An engine source checkout (an engine folder with Engine.sharpmake.cs), as opposed to the
    // engine files a cooked tree or an install carries.
    static bool IsEngineCheckout( const std::string& InDirectory );
    // The nearest engine at or above the executable's folder; empty when there is none.
    static std::string FindExecutableEngine();
};
