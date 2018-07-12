![Mitch Engine Banner](https://raw.githubusercontent.com/wobbier/MitchEngine/master/Docs/GitHub/me_banner.png)
======
The 3D game engine so good it has my name in it.

Mitch Engine is a simple c++ component based engine for building 3d games, demos, and other graphical applications.
It's a great hobby project to keep me exploring the world of c++.

Check out my [Trello Board][4] to check out the current development status.

[![Build status](https://ci.appveyor.com/api/projects/status/7x55po7se0siesdn?svg=true)](https://ci.appveyor.com/project/wobbier/mitchengine)
![license](https://img.shields.io/github/license/wobbier/mitchengine.svg)

The engine is:

  * Open source
  * Easy to use
  * Awesome

How to make a Mitch game
-----------------------

1. Pull the repo and run GenerateSolution.bat (Requires CMake for ThirdParty projects)
2. Make a Visual studio template of the example game.
3. Run the newly created project and start coding!
4. ????
5. Profit

```cpp
// Create an entity.
Entity TestEntity = GameWorld->CreateEntity();

// Add some components
Transform& TransformComponent = TestEntity.AddComponent<Transform>();
Sprite& SpriteComponent = TestEntity.AddComponent<Sprite>();
Collider2D& ColliderComponent = TestEntity.AddComponent<Collider2D>();
Animation& AnimationComponent = TestEntity.Addcomponent<Animation>();

// Start changing some values
TransformComponent.Position = glm::vec2(1.f, 0.5f);
SpriteComponent.SetSourceImage("Default.png");
AnimationComponent.SetAnimationInfo(SpriteComponent.Width, SpriteComponent.Height, 7, 4);
```

Main features
-------------
   * (ECS) Entity-Component System based design
   * Physics components (Bullet3D)
   * Scene management
   * Mouse/Keyboard on desktop
   * Language: C++
   * Open Source Commercial Friendly(MIT): Compatible with open and closed source projects
   * OpenGL 3.3 (desktop) based

Build Requirements
------------------

* Windows 10, VS 2017
* CMake - 3.12.0

Contributing to the Project
--------------------------------

Did you find a bug? Have a feature request?

  * [Contribute to the engine][2]

Contact me
----------

   * Website: [http://wobbier.com][1]
   * Twitter: [http://www.twitter.com/wobbier][3]

[1]: http://www.wobbier.com "My Portfolio"
[2]: https://github.com/wobbier/MitchEngine/issues "GitHub Issues"
[3]: http://www.twitter.com/wobbier "Twitter"
[4]: https://trello.com/b/QpR06bQl/mitchengine-status "Trello Board"
