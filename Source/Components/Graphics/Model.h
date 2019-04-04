#pragma once
#include "ECS/Component.h"
#include <string>
#include "imgui.h"

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

#if ME_EDITOR

	virtual void OnEditorInspect() final
	{
		ImGui::Text("Model ID:");
		ImGui::SameLine();
		ImGui::Text(std::to_string(Id).c_str());

		ImGui::Text("Path:");
		ImGui::SameLine();
		ImGui::Text(ModelPath.LocalPath.c_str());
	}

#endif
};