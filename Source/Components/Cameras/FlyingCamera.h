#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <string>

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
	float SpeedModifier = 100.f;

#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif
};
ME_REGISTER_COMPONENT(FlyingCamera)
