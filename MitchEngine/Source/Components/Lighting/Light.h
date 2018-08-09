#pragma once
#include "ECS/Component.h"
#include <glm.hpp>

class Light : public Component<Light>
{
public:

	Light() = default;
	~Light() = default;

	// Separate init from construction code.
	virtual void Init() final;

	glm::vec3 Colour = glm::vec3(1.f);
};