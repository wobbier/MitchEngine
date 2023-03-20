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

#if USING( ME_EDITOR )
	virtual void OnEditorInspect() final;
#endif

private:
	void OnSerialize(json& outJson) final;
	void OnDeserialize(const json& inJson) final;
};
ME_REGISTER_COMPONENT_FOLDER(FlyingCamera, "Misc")
