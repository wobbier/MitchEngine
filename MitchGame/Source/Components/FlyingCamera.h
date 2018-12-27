#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>

class FlyingCamera
	: public Component<FlyingCamera>
{
public:

	FlyingCamera();
	~FlyingCamera();

	// Separate init from construction code.
	virtual void Init() final;

	float FlyingSpeed = 5.f;

	float LookSensitivity = .15f;
	float SpeedModifier = 10.f;
};