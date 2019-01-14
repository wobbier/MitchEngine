#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>

namespace Moonlight
{
	class Shader;
	class FBXModel;
}

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