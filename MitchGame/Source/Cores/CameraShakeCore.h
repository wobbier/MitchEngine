#pragma once
#include "ECS/Core.h"
#include "Events/EventReceiver.h"
#include "PerlinNoise.hpp"

class CameraShakeCore
	: public Core<CameraShakeCore>
{
public:
	CameraShakeCore();
	~CameraShakeCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

private:
	float LastX = 0.f;
	float LastY = 0.f;
	bool FirstUpdate = true;
	float TotalTime = 0.f;
	siv::PerlinNoise Noise;
};
