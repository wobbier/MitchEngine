#pragma once
#include "Game.h"
#include "Cores/FlyingCameraCore.h"
#include <glm.hpp>

class TestGame
	: public Game
{
public:
	TestGame();
	virtual ~TestGame() override;

	virtual void Initialize() override;

	virtual void Update(float DeltaTime) override;

	virtual void End() override;

	FlyingCameraCore* FlyingCameraController;

	Entity MainCamera;
	Entity SecondaryCamera;
	std::vector<Entity> Cubes;
	bool AddedPhysics = false;

	glm::vec2 PrevMouseScroll;
};
