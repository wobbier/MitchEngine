![Mitch Engine Banner](https://raw.githubusercontent.com/wobbier/MitchEngine/master/.github/Havana.png)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/21af73bf73f049ac97afd48e1689fd33)](https://www.codacy.com/gh/wobbier/MitchEngine/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=wobbier/MitchEngine&amp;utm_campaign=Badge_Grade)
![license](https://img.shields.io/github/license/wobbier/mitchengine.svg)
======
The 3D game engine so good it has my name in it.

Mitch Engine is a simple c++ component based game engine for creating games for Xbox ( One & Series X/S ), Linux, macOS, Windows, and the Windows 10 Store ( UWP ).
It's a great hobby project to keep me exploring the world of c++.

Check out my [Notion][4] to view the current development status.

|Platform|Game|
|---|---|
|Linux|[![Build status Linux](https://github.com/wobbier/MitchEngine/actions/workflows/Linux.yml/badge.svg)](https://github.com/wobbier/MitchEngine/actions/workflows/Linux.yml)|
|Win64|[![Build status Win64](https://github.com/wobbier/MitchEngine/actions/workflows/Windows.yml/badge.svg)](https://github.com/wobbier/MitchEngine/actions/workflows/Windows.yml)|
|UWP / Xbox|[![Build status UWP](https://github.com/wobbier/MitchEngine/actions/workflows/UWP.yml/badge.svg)](https://github.com/wobbier/MitchEngine/actions/workflows/UWP.yml)|
|macOS|[![Build Status macOS](https://github.com/wobbier/MitchEngine/actions/workflows/macOS.yml/badge.svg)](https://github.com/wobbier/MitchEngine/actions/workflows/macOS.yml)|

Main Features
-------------
   * (ECS) Entity-Component System based design
   * Supports Linux, Windows and macOS (UWP / Xbox builds are kept compiling)
   * Havana, the world editor: undo, prefabs, play-in-editor snapshots, scripted regression flows
   * PBR renderer on [BGFX][9]: clustered lights, cascaded shadows, image-based lighting, fog, decals, TAA, shader graphs
   * [Box3D][7] and [Box2D][12] physics, Recast / Detour navigation, skeletal animation, terrain
   * C# scripting on .NET 8 with hot reload (See Below)
   * Language: C++20
   * Optional [FMOD][10] Audio (See Below)
   * HTML/CSS game UI with [RmlUi][5]: C# view models, hot reload, gamepad navigation, localization

How to make a Mitch game
-----------------------

**C# game (no C++):**

1. Clone this repo with its submodules (`git clone --recurse-submodules https://github.com/wobbier/MitchEngine`).
2. Run `GenerateSolution.bat` (Windows), `GenerateSolution.command` (macOS) or `GenerateSolution.sh` (Linux) from its root.
3. Open `MitchEngine.sln` in Visual Studio (Linux: open the folder in VS Code; macOS: `MitchEngine.xcworkspace` in Xcode), pick `Editor_Debug` and press F5. Havana opens the Project Hub: make a new project from a template anywhere you like.
4. Write C# under the project's `Assets/`; File > Build Game ships it. See [Docs/Projects.md](Docs/Projects.md#the-engines-own-solution).

C# scripting runs on Windows and Linux for now: macOS builds have no .NET host yet ([Docs/Scripting-DotNet.md](Docs/Scripting-DotNet.md)), so the editor opens there but a project's scripts don't run.

**C++ game:**

1. Run the project setup script from `Tools/` (`NewProjectSetup.bat`, `.command` or `.sh`) in an empty folder or your git repo, or start from the [MitchGame][13] example project. The engine becomes its `Engine/` submodule and the game builds its own editor.
2. Think of an awesome game idea.
3. ????
4. Profit

Examples
-----------------------
```cpp
// Grab the world
SharedPtr<World> GameWorld = GetEngine().GetWorld().lock();

// Create an entity.
EntityHandle MainCamera = GameWorld->CreateEntity();

// Add some components
Transform& CameraTransform = MainCamera->AddComponent<Transform>("Main Camera");
Camera& CameraComponent = MainCamera->AddComponent<Camera>();

// Start changing some values
CameraTransform.SetPosition(Vector3(0.f, 5.f, 10.f));

// Spawning models.
EntityHandle ModelEntity = GameWorld->CreateEntity();

ModelEntity->AddComponent<Transform>("Ground Model");
ModelEntity->AddComponent<Model>("Assets/Models/SuperAwesome.fbx");
```

Build Requirements
------------------

* Windows 10/11 with Visual Studio 2022
	* Desktop Development with C++
	* Game development with C++
	* Universal Windows Platform development (UWP builds only)
* Xcode for macOS
* Linux: Nix, which provides the whole toolchain and libraries (see [Docs/Development-Environment.md](Docs/Development-Environment.md))
* .NET 8 SDK for C# scripting
* CMake - 3.12.0 (Required if you wish to update ThirdParty projects)

Third Party Libraries
--------------------------------

  * [RmlUi][5] and FreeType
  * [Optick][6]
  * [Box3D][7] and [Box2D][12]
  * [BGFX][9]
  * [FMOD][10]
  * .NET 8 (hostfxr / nethost)
  * Recast / Detour, Assimp, SDL2, Dear ImGui

Enabling FMOD
--------------------------------

* Download and install the SDK from the [FMOD][10] website.
* Edit your project's `Game.sharpmake.cs` file in the root of your project and set the global field for `Globals.FMOD_****_Dir`
	* The default value for Windows is: `FMOD_Win64_Dir = "C:\\Program Files (x86)\\FMOD SoundSystem\\FMOD Studio API Windows\\"`
```cpp
[Generate]
public class SharpGameSolution : BaseGameSolution
{
    public SharpGameSolution()
        : base()
    {
        Name = "MyProjectName";
        Globals.FMOD_Win64_Dir = "C:/Program Files (x86)/FMOD SoundSystem/FMOD Studio API Windows/";
    }
}
```
* You can check the status of the FMOD plugin in the 'Help > About' menu in the editor.

C# Scripting (.NET 8)
--------------------------------

* Install the .NET 8 SDK. Game scripts are any `.cs` under your project's `Assets/`; the editor builds them and hot reloads them while you play.
* Windows: set `Globals.DOTNET_Win64_Dir` in your project's `Game.sharpmake.cs` to the `runtimes/win-x64/native` folder of the installed `Microsoft.NETCore.App.Host.win-x64` pack.
```cpp
[Generate]
public class SharpGameSolution : BaseGameSolution
{
    public SharpGameSolution()
        : base()
    {
        Name = "MyProjectName";
        Globals.DOTNET_Win64_Dir = "C:\\Program Files\\dotnet\\packs\\Microsoft.NETCore.App.Host.win-x64\\8.0.27\\runtimes\\win-x64\\native";
    }
}
```
* Linux: the Nix development shell sets everything up.
* See [Docs/Scripting-DotNet.md](Docs/Scripting-DotNet.md) for the API, the script lifecycle and hot reload.

Activity
--------------------------------
![Alt](https://repobeats.axiom.co/api/embed/381a3d4143660f089ee110663df36834bb7c7a62.svg "Repobeats analytics image")

[1]: https://www.mitch.gg "My Portfolio"
[2]: https://github.com/wobbier/MitchEngine/issues "GitHub Issues"
[3]: https://www.twitter.com/wobbier "Twitter"
[4]: https://www.notion.so/fc1126e1f8f54a9a8e0daf2735dc59ed?v=fb36a4a955b44efa8dda9fc84d743b43 "Notion"
[5]: https://github.com/mikke89/RmlUi "RmlUi"
[6]: https://github.com/bombomby/optick "Optick"
[7]: https://github.com/erincatto/box3d "Box3D"
[9]: https://github.com/bkaradzic/bgfx "BGFX"
[10]: https://www.fmod.com/ "FMOD Audio"
[12]: https://github.com/erincatto/box2d "Box2D"
[13]: https://github.com/wobbier/MitchGame "MitchGame example project"
