#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"
#include "RenderCommands.h"

class Light
	: public Component<Light>
{
public:
	Light();
	~Light() = default;

	// Separate init from construction code.
	virtual void Init() final;

	Vector3 Colour{ 1.f, 1.f, 1.f };
#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif
	Moonlight::LightCommand cmd;

private:
	void OnSerialize(json& outJson) override;
	void OnDeserialize(const json& inJson) override;
};
ME_REGISTER_COMPONENT_FOLDER(Light, "Rendering")
