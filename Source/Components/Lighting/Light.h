#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"

class Light : public Component<Light>
{
public:

	Light() = default;
	~Light() = default;

	// Separate init from construction code.
	virtual void Init() final;

	Vector3 Colour{ 1.f, 1.f, 1.f };
};
ME_REGISTER_COMPONENT(Light)
