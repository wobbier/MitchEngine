#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>
#include "Graphics/Shader.h"
#include "Graphics/FBXModel.h"

class Model : public Component<Model>
{
public:
	Model(std::string const &path, const std::string& shader, bool gamma = false);
	~Model();

	// Separate init from construction code.
	virtual void Init() final;

	Moonlight::FBXModel* ModelResource = nullptr;
	Moonlight::Shader* ModelShader = nullptr;
private:
	bool EnableGammaCorrection;
};