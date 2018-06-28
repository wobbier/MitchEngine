#pragma once
#include "Engine/Component.h"

class DebugCube
	: public Component<DebugCube>
{
public:
	DebugCube() = default;
	~DebugCube() = default;

	virtual void Init() final;

	void DrawCube();
	unsigned int VAO;
	unsigned int VBO;
	unsigned int EBO;
};
