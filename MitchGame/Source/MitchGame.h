#pragma once
#include <DirectXMath.h>
#include "Game.h"
#include <glm.hpp>
#include "Cores/FlyingCameraCore.h"

class MitchGame
	: public Game
{
public:
	MitchGame();
	virtual ~MitchGame() override;

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
