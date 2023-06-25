![Mitch Engine Banner](https://raw.githubusercontent.com/wobbier/MitchEngine/master/.github/Havana.png)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/21af73bf73f049ac97afd48e1689fd33)](https://www.codacy.com/gh/wobbier/MitchEngine/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=wobbier/MitchEngine&amp;utm_campaign=Badge_Grade)
![license](https://img.shields.io/github/license/wobbier/mitchengine.svg)
======
The 3D game engine so good it has my name in it.

Mitch Engine is a simple c++ component based game engine for creating games for Xbox One, macOS, and the Windows 10 Store.
It's a great hobby project to keep me exploring the world of c++.

Check out my [Notion][4] to view the current development status.

|Platform|Game|
|---|---|
|Win64|[![Build status Win64](https://github.com/wobbier/MitchEngine/actions/workflows/Windows.yml/badge.svg)](https://github.com/wobbier/MitchEngine/actions/workflows/Windows.yml)|
|UWP / Xbox|[![Build status UWP](https://github.com/wobbier/MitchEngine/actions/workflows/UWP.yml/badge.svg)](https://github.com/wobbier/MitchEngine/actions/workflows/UWP.yml)|
|macOS|[![Build Status](https://github.com/wobbier/MitchEngine/actions/workflows/macOS.yml/badge.svg)](https://github.com/wobbier/MitchEngine/actions/workflows/macOS.yml)|

Main Features
-------------
   * (ECS) Entity-Component System based design
   * Supports Windows, Xbox, and macOS
   * World Editor (Windows & macOS)
   * HTML UI with [Ultralight][5]
   * Language: C++
   * [Optick][6] Profiling
   * Optional [FMOD][10] Audio (See Below)
   * Optional/Experimental [MONO][11] Scripting Support (See Below)

How to make a Mitch game
-----------------------

1. Download the [Project Setup][8] batch file (Windows only atm, but generates macOS .command) and run within an empty folder or your premade git repo.
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

* Windows 10 or macOS
* Visual Studio 2019 for Windows / Xbox
	* Desktop Development with C++
	* Universal Windows Platform development
	* Game development with C++
	* C++ Universal Windows Platform tools
* XCode for macOS
* CMake - 3.12.0 (Required if you wish to update ThirdParty projects)

Third Party Libraries
--------------------------------

  * [Ultralight][5]
  * [Optick][6]
  * [Bullet Physics][7]
  * [BGFX][9]
  * [FMOD][10]
  * [MONO][11]

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

Enabling MONO Scripting (Experimental)
--------------------------------

* Download and install the SDK from the [MONO][11] website.
* Edit your project's `Game.sharpmake.cs` file in the root of your project and set the global field for `Globals.MONO_****_Dir`
	* The default value for Windows is: `MONO_Win64_Dir = "C:/Program Files/Mono/"`
	* The default value for macOS is: `MONO_macOS_Dir = "/Library/Frameworks/Mono.framework"`
```cpp
[Generate]
public class SharpGameSolution : BaseGameSolution
{
    public SharpGameSolution()
        : base()
    {
        Name = "MyProjectName";
        Globals.MONO_Win64_Dir = "C:/Program Files/Mono/";
	Globals.MONO_macOS_Dir = "/Library/Frameworks/Mono.framework";
    }
}
```
* You can check the status of the MONO plugin in the 'Help > About' menu in the editor.

Activity
--------------------------------
![Alt](https://repobeats.axiom.co/api/embed/381a3d4143660f089ee110663df36834bb7c7a62.svg "Repobeats analytics image")

[1]: https://www.mitch.gg "My Portfolio"
[2]: https://github.com/wobbier/MitchEngine/issues "GitHub Issues"
[3]: https://www.twitter.com/wobbier "Twitter"
[4]: https://www.notion.so/fc1126e1f8f54a9a8e0daf2735dc59ed?v=fb36a4a955b44efa8dda9fc84d743b43 "Notion"
[5]: https://github.com/ultralight-ux/Ultralight "Ultralight"
[6]: https://github.com/bombomby/optick "Optick"
[7]: https://github.com/bulletphysics/bullet3 "Bullet 3D"
[8]: https://raw.githubusercontent.com/wobbier/MitchEngine/master/NewProjectSetup.bat "Project Creator"
[9]: https://github.com/bkaradzic/bgfx "BGFX"
[10]: https://www.fmod.com/ "FMOD Audio"
[11]: https://www.mono-project.com/ "MONO Scripting"
