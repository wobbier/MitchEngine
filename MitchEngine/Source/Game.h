// 2018 Mitchell Andrews
#pragma once
#include "Engine/World.h"
#include "Resource/ResourceCache.h"
#include "Config.h"
#include "Engine/Clock.h"
#include "Engine/MitchEngine.h"

class IWindow;
class JobScheduler;

class Game
{
public:

	Game();
	Game(JobScheduler* scheduler);
	virtual ~Game();
	ME_NONCOPYABLE(Game)
	ME_NONMOVABLE(Game)

	void Start();
	void Run();

	virtual void Initialize();
	virtual void Update(float DeltaTime);
	virtual void End();

	static MitchEngine& GetEngine() { return m_engine; }
private:
	static MitchEngine m_engine;
	JobScheduler* m_scheduler;
};
