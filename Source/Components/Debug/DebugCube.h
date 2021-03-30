#pragma once
#include "ECS/Component.h"

class DebugCube
	: public Component<DebugCube>
{
public:
    DebugCube() : Component<DebugCube>("DebugCube") {}
	~DebugCube() = default;

	virtual void Init() final;

	void DrawCube();
	unsigned int VAO;
	unsigned int VBO;
	unsigned int EBO;
};
