# Projects

A **project** is a folder: `Assets/` plus `Project/Game.meproj`. A C++ game (MitchGame, Drumsmith,
`Tools/ProjectTemplate`) also carries the engine as its `Engine/` submodule and builds its own
editor and game. A **C#-only project** carries neither: any built editor opens it, the generic
**Player** runs it, and both compile its scripts with `dotnet build`. Nothing in it is C++. This is
the SHIP-4 step of the shipping track.

Code: `Source/Engine/Project.h` / `.cpp` (finding the project and its engine), the VFS `Engine/`
mount (`Modules/Dementia/Source/VFS/VFS.cpp`, `Docs/VFS.md`), `ScriptEngine::GetAssemblyDirectory`
(`Source/Scripting/ScriptEngine.cpp`), the Player (`Modules/Player/`) and File > Open Project
(`Modules/Havana/Source/Widgets/OpenProjectWidget.cpp`, `EditorApp::RequestOpenProject`).
The sample is `Samples/ScriptOnly/`. Proof: `Tests/Source/ProjectTests.cpp` and the
"C#-only project" step of `Tools/RunChecks.sh` (`Tests/Samples/ScriptOnly/`).

## What's in a project

| Path | What it is |
|---|---|
| `Project/Game.meproj` | JSON, `{"ProjectName": "..."}`: marks the folder as a project and names it (and the C++ solution) |
| `Assets/` | Scenes, scripts, content. `Assets/Config/ProjectSettings.json` holds the product identity and `Product.StartupScene` |
| `Project/Game.Script.csproj` | The C# scripts: every `.cs` under `Assets/`, against the engine's `ScriptCore` (see below) |
| `Engine/`, `Game.sharpmake.cs`, `Game/Source/`, `Game_EntryPoint/` | Only in C++ projects: the engine submodule and the game module |
| `.tmp/` | Everything derived or per user (script builds, caches, editor layout and prefs); ignore it |

Sharpmake generation (`Engine.sharpmake.cs`, `BaseGameSolution`) creates `Project/Game.meproj`, or
updates its `ProjectName` when the solution is renamed, and leaves every other key alone.
`Tools/BuildProject.sh` and `Tools/GenerateProject.sh` read the solution name from it.

## Opening one

```bash
# The editor, on any project (a folder, or its Project/Game.meproj)
./.build/Editor_Debug/Havana --project Engine/Samples/ScriptOnly

# The Player: boots Product.StartupScene, like a game build (--scene overrides it)
./.build/Game_linux_Debug/Player --project Engine/Samples/ScriptOnly
```

`--project` changes into the project before anything resolves a path
(`Project::InitFromCommandLine`, first thing in `ME_APPLICATION_MAIN` and in Havana's `main`), so
every mode works on it, `--cook` and `--build-game` included. Without `--project` the project is
the working directory, as it always was. A folder that isn't a project fails with a message and
exit code 1 (2 for Havana).

When the editor has no last scene for a project (its first open) or that scene is gone, it opens
`Product.StartupScene`.

**File > Open Project...** opens a folder browser that starts beside the open project and lists
project folders first. **Open** (or a double-click on a project) reopens the editor on it,
`Havana --project <folder>`, after the unsaved-changes prompt. Switching always starts a fresh
editor process, so nothing of the old project (assets, scripts, caches) carries over. Unattended
runs only log the command they would run (`Open Project: would reopen the editor with ...`); the
`open-project <folder>` edscript command drives it.

## Which engine

`Project::InitFromCommandLine` picks the engine and hands both roots to
`VFS::SetDefaultRoots`:

1. `<project>/Engine`, when it is an engine (holds the engine-only
   `Assets/Config/HardwareProfiles.json`, `Project::IsEngineDirectory`). That is every C++ project,
   and nothing about them changed.
2. Otherwise, if the folder has a `Project/Game.meproj`, the **running executable's engine**
   (`Project::FindExecutableEngine`): the nearest `Engine/` at or above the executable's folder, so
   `<checkout>/.build/Editor_Debug/Havana` uses `<checkout>/Engine`. An engine checkout's own
   builds find the checkout itself.

Folders without a `.meproj` (a cooked tree, a shipped build, an empty directory with `--pak`)
never take the second rule, so they never pick up engine files from wherever the executable sits.

With the engine outside the project, the VFS mounts it at the `Engine/` folder prefix as well as
under the overlay (`Docs/VFS.md`), so `Engine/...` paths (shaders, tools, engine assets) resolve
exactly as they do through a project's own `Engine/`. The few places that walk folders on disk
resolve them through `Path` first: the asset database scan, the asset watcher, the asset browser's
**Engine Assets** tree (its paths still read `Engine/Assets/...`). The log says which engine
started: `Project: <root> (engine: <engine>)`.

An executable built outside a project that has C++ game code (`Project::HasGameModule`) warns at
startup: that project's components and cores live in its own builds.

## Scripts

A project's own builds (`<project>/.build/<config>/`) keep the script assemblies next to the
executable. A **shared** executable, another checkout's editor or the Player, builds a project's
scripts into `<project>/.tmp/Scripts/<Executable>-<Debug|Release>/` instead
(`ScriptEngine::GetAssemblyDirectory`). That's one folder per executable, so the editor and the
Player can run the same project at once. The folder's `Engine.stamp` names the engine it was built
against, and opening the project with another engine rebuilds it.

The script project finds `ScriptCore` through the MSBuild property `MitchEngineDir`, which defaults
to the project's `Engine/`. The engine always passes its own (`-p:MitchEngineDir=<engine>/`), and
when that isn't the project's `Engine/` it also writes `.tmp/MitchEngine.props`, which the script
project imports, so IDEs opening `Game.Script.csproj` resolve it too. The sample's
`Project/Game.Script.csproj` is the template for a C#-only project's. Details in
`Docs/Scripting-DotNet.md`.

## The Player

`Modules/Player/` is the engine with no game module: `PlayerGame` (`Modules/Player/Source/main.cpp`) opens an
empty scene and the engine boots the startup scene. Every game solution builds it beside
`Game_EntryPoint` (`make -f <Solution>.make Player config=game_linux_debug`, output
`.build/Game_<platform>_<Config>/Player`); UWP solutions don't. It takes the game's command line
(`--scene`, `--frames`, `--screenshot`, `--pak`, ...), and its debug and release builds compile the
project's scripts as the editor does.

## The sample

`Samples/ScriptOnly/` is the smallest C#-only project: one scene (`Samples/ScriptOnly/Assets/Scenes/Main.lvl`,
also its startup scene), one script (`Samples/ScriptOnly/Assets/Scripts/Spinner.cs`), its `Engine.cfg`, settings and the two
project files. Copy the folder anywhere to start a project.

## Proof

- `Tests/Source/ProjectTests.cpp`: project roots from folders and `.meproj` paths, finding the
  executable's engine, an engine outside the project serving every `Engine/...` spelling (Raw and
  overlay lookups, enumeration, recorded spellings, writes), and no `Engine/` mount for a
  project's own engine.
- RunChecks' **C#-only project** step copies the sample out of the repo beside a second copy. In
  the editor, `Tests/Samples/ScriptOnly/ScriptOnlyFlows.edscript` checks the startup scene opened,
  plays the script (the cube spins and bobs), and asks File > Open Project for the sibling. The
  step then checks the editor built the scripts into the copy, and the Player's capture of
  `Main.lvl` must match `Tests/Samples/ScriptOnly/Reference/Main.png` (byte-identical run to run).

## Not yet

- **Build Game** still compiles the project's own `Game_EntryPoint`, so it can't ship a C#-only
  project yet (ME-266: package the Player instead).
- The editor carries the game module of the checkout it was built in: a C#-only project opened in
  MitchGame's Havana lists MitchGame's components too. Prebuilt binaries (SHIP-7) would ship an
  editor without one.
- No recent-projects list or new-project creation; that is the Hub (SHIP-5) and the templates
  (SHIP-6). The standalone `Tools/HUB` still reads `Project/Game.meproj` the old way.
- A C#-only project needs the engine's sources: its script project references
  `Modules/ScriptCore/ScriptCore.csproj`. A prebuilt engine would ship `ScriptCore.dll` to
  reference instead.
- Written portably (SDL's base path, `std::filesystem`), verified on Linux only.

## Related Docs

- `Docs/VFS.md`: the mount table, including the `Engine/` folder mount.
- `Docs/Scripting-DotNet.md`: script builds and the assembly directory.
- `Docs/Building.md`, `Docs/Cooking.md`: shipping a project.
- `Docs/Development-Environment.md`: building the editor and the Player.
