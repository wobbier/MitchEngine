# Building a Game (Build Game)

**Build Game** turns the project into a folder players can run: the game executable in its
shipping configuration, the C# scripts, the cooked content in one pak, a private copy of .NET,
the libraries the executable needs and a launcher. The output runs from any directory on a
machine with no .NET install and no engine sources. It is the SHIP-3 step of the build pipeline,
on top of the cooker (`Cooking.md`). A C#-only project (`Projects.md`) builds the same way and
ships the generic **Player** as its executable (ME-266).

Code: `Source/Tools/Build/BuildGame.h` / `.cpp`; the profiles live in `ProjectSettings`
(`Source/Engine/ProjectSettings.h`); the editor window is `Modules/Havana/Source/Widgets/BuildGameWidget.cpp`. The runtime side is in `Source/Engine/Engine.cpp` (the
shipped pak and the window icon) and `Source/Scripting/ScriptHost.cpp` (the app-local runtime).
Proof: the "Build Game" step of `Tools/RunChecks.sh`, and `BuildGameFlows.edscript` (game repo)
for the window.

## In the editor

**File > Build Game...** (Ctrl+Shift+B) opens the Build Game window:

- **Profiles:** the project's build profiles on the left, the selected one's settings on the right.
  Add, Duplicate and Delete edit the list, and **Save Profiles** writes them to Project Settings.
- **Options:** *Compile the game* and *Record scenes*. Recording opens a small game window for a
  few seconds per scene.
- **Build:** runs `Havana --build-game --profile <file> --verbose` as a child process. The
  profile it builds is the one shown, saved or not. The editor stays responsive, and a crash in
  the build can't take it down.
- **While it runs:** a progress bar follows the five steps, and the output streams into the
  window (asset-load lines filtered out, errors and warnings coloured). **Cancel** stops the
  build after its current step.
- **When it's done:** **Open Folder** and **Run** (the build's launcher), or the reason it failed.
  The editor log records the outcome (`Build Game: '<profile>' succeeded -> <folder>`).

## Running it

```bash
# Build the project's first profile (or the defaults) into Builds/<Profile>.
Engine/Tools/Headless.sh ./.build/Editor_Debug/Havana --build-game

# Build two showcases somewhere else, then run the result from anywhere.
Engine/Tools/Headless.sh ./.build/Editor_Debug/Havana --build-game --scene Assets/Scenes/Showcase/Lighting.lvl \
    --scene Assets/Scenes/Showcase/Playground.lvl --out .tmp/BuildGame/Out
.tmp/BuildGame/Out/MitchGame.sh --scene Assets/Scenes/Showcase/Lighting.lvl
```

| Option | Meaning |
|---|---|
| `--build-game [<profile>]` | Build this profile (Project Settings > Build). Without a name: the first profile, or the defaults when there is none |
| `--profile <file.json>` | Build the profile in this file instead (one `Build.Profiles` entry; the editor window hands its profile over this way) |
| `--out <dir>` | Where the build goes (default: the profile's, else `Builds/<Profile>`) |
| `--scene <lvl>` (repeatable) | The scenes to ship (default: the profile's, else the project's `Build.Scenes` plus its `StartupScene`) |
| `--configuration Retail\|Release\|Debug` | The game configuration (default `Retail`). Release and Debug keep the tools code (automation, cooking) |
| `--no-compile` | Package the configuration's existing executable (`.build/Game_<platform>_<Configuration>/`) instead of building it |
| `--loose` | Ship the cooked tree as files instead of `Game.mpak` |
| `--archive` | Also pack the output into `<dir>.tar.gz` (`.zip` on Windows) next to it |
| `--game <exe>` / `--no-record` | The tools game build the cooker records scenes with / a static cook only. Default: the tools game matching this editor build (Release next to a Release editor, else Debug), compiled along with the game; with `--no-compile`, the newest one there |
| `--verbose` | Print every line of compiler and tool output. It always goes to `.tmp/BuildGame/Staging/<Profile>/Build.log` |
| `--cancel-file <file>` | Stop after the current step once this file exists |

`Havana --build-game` exits non-zero when the build failed. The cook records each scene through
the game, so run it under `Tools/Headless.sh` in automation.

## Profiles

A build profile is one way to build the project. Profiles are saved in
`Assets/Config/ProjectSettings.json` under `Build.Profiles`:

| Field | Default | Meaning |
|---|---|---|
| `Name` | `Default` | Picks it on the command line; names the default output folder |
| `Configuration` | `Retail` | `Retail` ships. `Release` and `Debug` keep the tools code |
| `Output` | `""` (`Builds/<Name>`) | Where the build goes. It is replaced each build |
| `Scenes` | `[]` | The scenes to ship. Empty: `Build.Scenes` and the `StartupScene` |
| `Pak` | `true` | The content in one `Game.mpak` (true) or as a loose cooked tree |
| `Archive` | `false` | Also a `.tar.gz` (`.zip` on Windows) of the output |

Builds are made for the machine building them: a Linux editor builds Linux games.

## The steps

1. **Compile.** On Linux, `make -f <Solution>.make Game_EntryPoint config=game_linux_<configuration>`
   through the generated solution makefile. When the cook records scenes and no `--game` was
   named, the tools game it records through (the editor's configuration) is compiled too, so an
   out-of-date one can't fail the cook. A C#-only project has no solution or game module
   (`Project::HasGameModule`), so it ships the **Player** instead: `make -C <checkout> -f
   <Solution>.make Player config=game_linux_<configuration>` in the checkout this editor was
   built in (the parent of its `.build/`), output `<checkout>/.build/Game_<platform>_<Configuration>/Player`.
   The cook records through that checkout's tools Player, run in the project. Windows and macOS need `--no-compile` and an IDE build for
   now (ME-265).
2. **Scripts.** `dotnet build Project/Game.Script.csproj -c Release` (`Debug` for Debug builds)
   into the staging folder, with `-p:MitchEngineDir=<engine>/` so the script project finds this
   engine's `ScriptCore` (a C#-only project has no `Engine/`). The assemblies, their `.deps.json` and `ScriptCore.runtimeconfig.json`
   ship next to the executable. The source generator (`MitchEngine.Generators`) stays behind, and
   so do `.pdb` files in Retail. A project without a script project ships without scripts.
3. **Cook.** The SHIP-2 cooker (`Cooking.md`) on the profile's scenes, recording each through the
   tools game build. With `Pak` the cooker packs the tree straight into `<out>/Game.mpak`;
   otherwise the cooked tree (`Assets/`, `AssetRegistry.json`, `CookManifest.json`) is copied in.
4. **Runtime.** An **app-local .NET**: the build machine's install (`ScriptHost::FindSystemDotnetRoot`,
   the same lookup the runtime uses: `DOTNET_ROOT`, then the OS defaults) is copied to
   `<out>/dotnet/`. That copy has `host/fxr/<newest>/` plus the newest `shared/Microsoft.NETCore.App/`
   of the major version `ScriptCore.runtimeconfig.json` names, and .NET's `LICENSE.txt` and
   `ThirdPartyNotices.txt`. It is about 71 MiB.
5. **Package.**
   - The executable is copied as `<Product Name>.x86_64` (Linux), `<Product Name>.exe`
     (Windows) or `<Product Name>` (macOS).
   - **Linux:** `ldd` picks the libraries a distro doesn't provide, or provides in other
     versions: SDL2, `libstdc++`, `libgcc_s`, `libnethost` and anything linked from the
     tree the executable was built in (FMOD): the project, or the Player's checkout. These go into `lib/`, and `<Product Name>.sh` launches the game with
     them. glibc, the GL/Vulkan drivers, X11/Wayland, ALSA and curl are left to the system.
   - **Windows:** the DLLs the build copied next to the executable ship beside it.
   - The product icon is copied as `icon.<ext>`.
   - `BuildInfo.json` records the product, profile, configuration, platform, launcher, content
     and .NET version.
   - The optional archive is made with `tar`.

The output folder is only ever replaced when it holds an earlier build (its `BuildInfo.json`) or
nothing at all. Intermediates (the script build, the cooked tree, `Build.log`) stay in
`.tmp/BuildGame/Staging/<Profile>/`.

## What ships

```text
Builds/Default/
  MitchGame.sh                 launcher (Linux): cd to its folder, lib/ on LD_LIBRARY_PATH
  MitchGame.x86_64             the Retail game executable
  Game.mpak                    the cooked content (or Assets/ + AssetRegistry.json with --loose)
  Game.Script.dll, ScriptCore.dll, *.deps.json, ScriptCore.runtimeconfig.json
  dotnet/host/fxr/8.0.31/      app-local .NET: the host...
  dotnet/shared/Microsoft.NETCore.App/8.0.31/   ...and the runtime
  lib/                         libSDL2, libstdc++, libgcc_s, libnethost, FMOD (Linux)
  icon.png                     when the product has an icon
  BuildInfo.json
```

## At runtime

- **Content:** with no `--pak` on the command line, the game mounts `Game.mpak` from beside its
  executable (`CookedAssets::kShippedPak`) at the same priority as `--pak`. The pak carries
  `AssetRegistry.json`, so the run is a cooked run (`Cooking.md`). It logs
  `Content: mounting <pak>`.
- **Scripts:** `ScriptEngine` hands `<exe dir>/dotnet` to `ScriptHost::Init` when it holds
  `host/fxr`, so hostfxr and the framework both come from the build. The log says
  `Scripts: .NET runtime from the app-local install (...)` (`Scripting-DotNet.md`).
- **Window icon:** `Engine::Init` decodes the product icon (Project Settings > Product > Icon)
  with bimg and sets it on the window. The cooker ships the icon's source bytes for this.
- **User state** goes to the OS per-user folder in Retail builds (`VFS.md`, `user://`). Logs
  and crash dumps still land in the working directory (ME-263).

## Proof

The "Build Game" step of `RunChecks.sh` builds the Lighting, Playground and RenderTexture
showcases as a Retail game into `.tmp/BuildGame/Checks`, so camera render textures are proven in
a shipped build too. It copies the build to a temporary folder outside the repo and captures the
three scenes from an empty working directory. The captures run with `DOTNET_ROOT` unset
and the repo's library paths removed from `LD_LIBRARY_PATH`. They must match the showcase
references, and every run's log must show the app-local runtime. The project's main menu
(`Assets/Main.lvl`) ships too, and must run for 60 frames: its scripts draw ImGui debug windows,
which crashed Retail builds until those calls learned to do nothing without ImGui.

The "C#-only project" step does the same for `Samples/ScriptOnly`, copied out of the repo:
`Havana --project <copy> --build-game` ships the Retail Player, and the build, moved beside the
copy and started from an empty directory the same way, must match
`Tests/Samples/ScriptOnly/Reference/Main.png` (0.2% limit; the runs are byte-identical, and the
scene without its script is 1.4% off) with the log showing the app-local runtime and
`Game.Script.dll` loaded. It is 124 MiB: a 46 MiB Player, a 1 MiB pak, .NET and the libraries.

Results on Linux (2026-10-25):

- **Size:** the build takes 20 s with a warm compiler cache and is 192 MiB: a 47 MiB
  executable, a 69 MiB pak, 71 MiB of .NET and 6 MiB of libraries.
- **Captures:** they match the references as closely as the source-tree runs do.
- **Under `steam-run`:** run from a plain shell (an FHS environment with no dev shell,
  `DOTNET_ROOT` or `LD_LIBRARY_PATH`), the capture is byte-identical to the one above.
- **Without the runtime:** with the `dotnet/` folder removed, the same build fails to start
  .NET (`get_hostfxr_path` 0x80008083). The app-local copy is what runs the scripts.

## Not yet

- **Portable Linux binaries:** a build made in the Nix dev shell links against the Nix glibc
  (its interpreter is a `/nix/store` path). It runs on NixOS, and under `steam-run` there. Other
  distros and the Steam Deck need the build made inside the Steam Runtime (SHIP-8).
- **Windows and macOS:** no compile step yet, and no `.exe` icon resource or `.app` bundle
  (ME-265, SHIP-8).
- **Install-folder writes:** see ME-263.
- **C#-only projects need the editor's checkout:** the Player is compiled there through its
  solution makefile. An editor without one (an install, SHIP-7) can only package a Player that
  is already built beside it (`--no-compile`).
- **Engine CI artifacts** still use the older `Tools/PackageLinuxBuild.sh`, which is kept for
  the editor packages until SHIP-7 (ME-264).
