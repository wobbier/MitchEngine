// 2018 Mitchell Andrews
#pragma once
#include "Dementia.h"

class Game
{
public:
	Game() = default;
	virtual void OnInitialize() = 0;

	virtual void OnStart() = 0;
	virtual void OnUpdate(float DeltaTime) = 0;
	virtual void OnEnd() = 0;
	virtual void PostRender() = 0;
	class Engine* mEngine = nullptr;
	ME_NONCOPYABLE(Game)
	ME_NONMOVABLE(Game)
};
