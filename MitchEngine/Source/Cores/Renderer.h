#pragma once
#include "Engine/Core.h"
#include "Components/Sprite.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

class Renderer :
	public Core<Renderer>
{
public:
	Renderer();
	~Renderer();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	void Render();

	unsigned int VAO;
	unsigned int VBO;
	unsigned int EBO;
};
