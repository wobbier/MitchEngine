#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/Shader.h"

#if ME_OPENGL

#include <glad/glad.h>
#include <GLFW/glfw3.h>
class DifferedLighting :
	public Core<DifferedLighting>
{
public:
	DifferedLighting();
	~DifferedLighting();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	void PostRender();
	void PreRender();

	Shader* LightingPassShader = nullptr;

	std::vector<glm::vec3> lightPositions;
	unsigned int gBuffer;
	std::vector<glm::vec3> lightColors;
	unsigned int gPosition, gNormal, gAlbedoSpec;
};

#endif