#pragma once
#include <DirectXMath.h>
#include "Game.h"
#include "Cores/FlyingCameraCore.h"
#include "Cores/CameraShakeCore.h"
#include "Math/Vector3.h"

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
	CameraShakeCore* CameraShakeController;

	Entity MainCamera;
	Entity SecondaryCamera;
	std::vector<Entity> Cubes;
	bool AddedPhysics = false;

	Vector2 PrevMouseScroll;
};
