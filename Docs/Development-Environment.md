# Linux development environment

The engine SDK owns `nix/flake.nix`, its lock, and the shared shell definition.
The game selects an engine revision and can optionally add dependencies in its
own `devshell.nix`. New games need no Nix files or engine-repository changes.
The current Linux SDK and its prebuilt libraries target x86_64.

## VS Code and command line

Install Nix and VS Code's C/C++ extension (`ms-vscode.cpptools`), initialize the
engine's submodules, and open the game folder. The project template includes
Linux F5 configurations for Havana and the game. Their prelaunch task uses
`Tools/BuildProject.sh`, which enters the SDK environment, generates third-party
headers if missing, regenerates the project, and builds the selected target.
The debugger uses `Tools/NixGdb.sh` and the same environment. VS Code can start
from the desktop; it does not need to inherit a development terminal.

From a game root:

```bash
Engine/Tools/DevShell.sh
Engine/Tools/BuildProject.sh Havana editor_debug
Engine/Tools/BuildProject.sh Game_EntryPoint game_linux_release
Engine/Tools/NixEnv.sh ./.build/Editor_Debug/Havana
```

`BuildProject.sh` takes a target, configuration, and optional extra make
arguments. It reads the generated solution name from `Project/Game.meproj`.
`MITCH_BUILD_JOBS` overrides the default `nproc` job count.
`Tools/GenerateProject.sh` generates without building; `--compile-commands`
retains the bear-based editor build used to generate `compile_commands.json`.
Project launchers under `Project/` delegate to this implementation.

`DevShell.sh --command COMMAND ...` and `NixEnv.sh COMMAND ...` execute a command
without changing its working directory. Pass `--project /path/to/game` before
the command to select a game explicitly. This also supports an SDK installed
outside the game repository:

```bash
/opt/MitchEngine/Tools/DevShell.sh --project "$PWD"
/opt/MitchEngine/Tools/NixEnv.sh --project "$PWD" make -f MyGame.make
```

The environment launchers support a separate SDK installation. The existing
Sharpmake project templates still expect an `Engine/` directory; changing that
build-system convention is separate work. The SDK's Nix support files can ship
with a binary SDK and do not require access to its C++ source repository.

## Private project extensions

Create `devshell.nix` in the **game's** repository only when extra dependencies
are needed. It receives the SDK's pinned `pkgs` and returns optional additions:

```nix
{ pkgs }:
{
  packages = [ pkgs.nodejs_26 ];
  buildInputs = [ pkgs.alsa-lib ];
  runtimeLibraries = [ pkgs.alsa-lib ];
  env = { MY_GAME_SETTING = "enabled"; };
  shellHook = ''
    export MY_GAME_ASSETS="$MITCH_PROJECT_ROOT/Assets"
  '';
}
```

Supported additions are `packages`, `nativeBuildInputs`, `buildInputs`,
`runtimeLibraries`, `env`, and `shellHook`. Runtime libraries extend
`LD_LIBRARY_PATH`; Nix's `makeLibraryPath` does not collect transitive libraries.
The engine runs its hook first and the project's hook second. Required SDK
variables such as `DOTNET_LINUX_NATIVE_DIR` are initialized by the SDK.

No configuration file means the default engine environment. Extensions stay
with private games; there is no registry of game profiles in the engine. The
launcher imports the local configuration using `--impure`; the package set
remains pinned by the engine's lock. A project that fetches additional external
dependencies should pin those sources in its own extension.

## Ownership and maintenance

`Tools/DevShell.sh` resolves the SDK and project paths at runtime. Local FMOD
libraries come from the live SDK, rather than an archived copy of the engine or
game. The command wrapper explicitly runs `shellHook` after `nix print-dev-env`
and suppresses hook stdout so GDB's MI protocol stays clean. Failed environment
evaluation aborts before any build or debugger starts. Commands keep their exit
status and receive arguments without shell re-parsing.

The base environment preserves the C++/.NET, SDL, X11/Wayland, graphics loader,
and GTK/Ultralight dependencies. It also provides Python, bear, patchelf, Git,
Xvfb, and the matching Mesa lavapipe ICD for engine tools. Host GPU driver
compatibility still needs testing when updating the lock; naming
`nixos-unstable` does not automatically update a locked package revision.

Update the SDK's dependencies in the engine repository:

```bash
nix flake update --flake path:./nix
nix flake check --no-build path:./nix
```

New-project setup and Linux CI use the same SDK launchers. Game repositories
and the project template no longer carry copied flakes or locks. Bare
`nix develop` in a game root is replaced by `Engine/Tools/DevShell.sh`.

Integration checks for the launcher and private extensions:

```bash
python3 -m unittest discover -s Engine/Tests/Tools
```
