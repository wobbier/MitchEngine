#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <string>
#include "imgui.h"
#include "Graphics/ShaderCommand.h"
#include "FilePath.h"

class Model
	: public Component<Model>
{
	friend class RenderCore;
public:
	Model();
	Model(const std::string& path);
	~Model();

	// Separate init from construction code.
	virtual void Init() final;

	std::shared_ptr<class ModelResource> ModelHandle = nullptr;
	class Moonlight::ShaderCommand* ModelShader = nullptr;

	unsigned int GetId();

	virtual void Deserialize(const json& inJson) final {
		ModelPath = FilePath(inJson["ModelPath"]);
	}
private:
	FilePath ModelPath;
	unsigned int Id = 0;
	bool IsInitialized = false;
#if ME_EDITOR

	virtual void OnEditorInspect() final
	{
		ImGui::Text("Model ID:");
		ImGui::SameLine();
		ImGui::Text(std::to_string(Id).c_str());

		ImGui::Text("Path:");
		ImGui::SameLine();
		ImGui::Text(ModelPath.LocalPath.c_str());
		//if (ModelHandle)
		//{
		//	for (int i = 0; i < ModelHandle->Meshes.size(); ++i)
		//	{
		//		if (ModelHandle->Meshes[i] && ModelHandle->Meshes[i]->material)
		//		{
		//			ImGui::Text("Mesh %i:", i);
		//			for (auto texture : ModelHandle->Meshes[i]->material->GetTextures())
		//			{
		//				if (texture != nullptr)
		//				{
		//					ImGui::Text("Texture Path: %s", texture->Directory);
		//				}
		//			}
		//		}
		//	}
		//}

		if (ModelShader)
		{
		}
	}

	virtual void Serialize(json& outJson) final
	{
		Component::Serialize(outJson);

		outJson["ModelPath"] = ModelPath.LocalPath;
	}

#endif
};

ME_REGISTER_COMPONENT(Model)