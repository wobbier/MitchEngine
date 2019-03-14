#pragma once
#include "ECS/Component.h"
#include <string>

namespace Moonlight
{
	class Shader;
}

class Model : public Component<Model>
{
	friend class RenderCore;
public:
	Model(const std::string& path);
	~Model();

	// Separate init from construction code.
	virtual void Init() final;

	std::shared_ptr<ModelResource> ModelHandle = nullptr;
	Moonlight::Shader* ModelShader = nullptr;

	unsigned int GetId();
private:
	FilePath ModelPath;
	unsigned int Id;
};