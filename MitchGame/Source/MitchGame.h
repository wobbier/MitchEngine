#pragma once
#include <DirectXMath.h>
#include "Game.h"
#include <glm.hpp>
#include "Cores/FlyingCameraCore.h"
#include "Cores/CameraShakeCore.h"

class JobScheduler;

class MitchGame
	: public Game
{
public:
	MitchGame();
	MitchGame(JobScheduler* scheduler);
	virtual ~MitchGame() override;

	virtual void Initialize() override;

	virtual void Update(float DeltaTime) override;

	virtual void End() override;

	FlyingCameraCore* FlyingCameraController;
	CameraShakeCore* CameraShakeController;

	Entity MainCamera;
	Entity SecondaryCamera;
	std::vector<Entity> Cubes;
	bool AddedPhysics = false;

	glm::vec2 PrevMouseScroll;
};
