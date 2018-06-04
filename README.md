![Mitch Engine Banner](https://raw.githubusercontent.com/wobbier/MitchEngine/master/Docs/GitHub/me_banner.png)
======
The 3D game engine so good it has my name in it.

Mitch Engine is a simple c++ component based engine for building 3d games, demos, and other graphical applications.
It's a great hobby project to keep me exploring the world of c++.

Check out my [Trello Board][5] to check out the current development status.

The engine is:

  * Open source
  * Easy to use
  * Awesome

How to make a Mitch game
-----------------------

1. Clone the repository and download the [Dependencies][2].
2. Place the `Config` folder in your project directory(or next to the .exe) and the `ThirdParty` folder to the project root.
3. Make a Visual studio template of the example game (or create a empty project and link the libraries).
4. Run the newly created project and start coding!

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
	ColliderComponent.SetBodyType(b2_dynamicBody);

	// Tell the world the entity is ready to be updated
	TestEntity.SetActive(true);
```

Main features
-------------
   * Component based design
   * Physics components (Bullet3D)
   * Scene management
   * Mouse/Keyboard on desktop
   * Language: C++, with json config files
   * Open Source Commercial Friendly(MIT): Compatible with open and closed source projects
   * OpenGL 3.3 (desktop) based

Build Requirements
------------------

* Windows 7+, VS 2013+
* GLEW - 1.11.0
* GLFW - 3.0.4
* JsonCpp - 1.0.0
* SOIL (Simple OpenGL Image Library) - Latest
* Bullet - Latest

Contributing to the Project
--------------------------------

Did you find a bug? Have a feature request?

  * [Contribute to the engine][3]

Contact me
----------

   * Website: [http://wobbier.com][1]
   * Twitter: [http://www.twitter.com/wobbier][4]

[1]: http://www.wobbier.com "My Portfolio"
[2]: https://dl.dropboxusercontent.com/u/14759830/Portfolio/MitchEngine/EngineDeps.7z "Download Dependencies"
[3]: https://github.com/wobbier/MitchEngine/issues "GitHub Issues"
[4]: http://www.twitter.com/wobbier "Twitter"
[5]: https://trello.com/b/QpR06bQl/mitchengine-status "Trello Board"
