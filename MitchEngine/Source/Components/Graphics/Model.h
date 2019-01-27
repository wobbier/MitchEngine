#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>

namespace Moonlight
{
	class Shader;
}

class Model : public Component<Model>
{
	friend class RenderCore;
public:
	Model(const std::string& path, const std::string& shader);
	~Model();

	// Separate init from construction code.
	virtual void Init() final;

	ModelResource* ModelHandle = nullptr;
	Moonlight::Shader* ModelShader = nullptr;

	unsigned int GetId();
private:
	FilePath ModelPath;
	FilePath ShaderPath;
	unsigned int Id;
};