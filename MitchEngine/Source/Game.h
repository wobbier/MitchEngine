// 2018 Mitchell Andrews
#pragma once
#include "Engine/World.h"
#include "Resource/ResourceCache.h"
#include "Config.h"
#include "Engine/Clock.h"
#include "Engine/Engine.h"

class IWindow;

class Game
{
public:

	Game();
	virtual ~Game();
	ME_NONCOPYABLE(Game)
	ME_NONMOVABLE(Game)

	void Start();
	void Run();

	virtual void Initialize();
	virtual void Update(float DeltaTime);
	virtual void End();

	static Engine& GetEngine() { return m_engine; }
private:
	static Engine m_engine;
};
