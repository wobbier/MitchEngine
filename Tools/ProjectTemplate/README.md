# MitchEngine game project

On Linux, install Nix and VS Code's C/C++ extension (`ms-vscode.cpptools`),
initialize the engine submodules, and open this folder in VS Code. Press F5
to build and debug Havana, or select Game in Run and Debug.

The engine supplies the environment and generates the build files. A new game
does not need its own flake, package lock, or Nix configuration.

```bash
Engine/Tools/DevShell.sh
Engine/Tools/BuildProject.sh Havana editor_debug
Engine/Tools/BuildProject.sh Game_EntryPoint game_linux_debug
Engine/Tools/NixEnv.sh ./.build/Editor_Debug/Havana
```

Optional dependencies belong in a `devshell.nix` file in this game's repository.
See [the SDK environment guide](Engine/Docs/Development-Environment.md) for the
extension API, editor integration, and command-line options.

Windows and macOS continue to use the platform's generator in `Project/`.
