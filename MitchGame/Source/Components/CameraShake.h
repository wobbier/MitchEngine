#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>
#include "Math/Vector3.h"

class CameraShake
	: public Component<CameraShake>
{
public:
	CameraShake();
	~CameraShake();

	// Separate init from construction code.
	virtual void Init() final;

	float ShakeAmount = 0.25f;

	Vector3 MaxDirection;
};