#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/Shader.h"

/*
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
	Cubemap* SkyboxMap = nullptr;
	Shader* SkyboxShader = nullptr;
	unsigned int skyboxVAO, skyboxVBO;
};
*/
