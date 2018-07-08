#pragma once
#include "Engine/Component.h"
#include <string>
#include <glm.hpp>

class SpotLight : public Component<SpotLight>
{
public:

	SpotLight() = default;
	~SpotLight() = default;

	// Separate init from construction code.
	virtual void Init() final;

	int LightVAO;
	int LightVBO;
};