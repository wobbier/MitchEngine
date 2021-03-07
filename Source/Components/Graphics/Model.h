#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <string>
#include "imgui.h"
#include "Graphics/ShaderCommand.h"
#include "Path.h"
#include <filesystem>
#include "Pointers.h"
#include "Scene/Node.h"
#include "ECS/EntityHandle.h"

class Entity;

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

	void RecursiveLoadMesh(Moonlight::Node& root, EntityHandle& parentEnt);

	SharedPtr<class ModelResource> ModelHandle = nullptr;
	class Moonlight::ShaderCommand* ModelShader = nullptr;

private:
	Path ModelPath;
	bool IsInitialized = false;

	virtual void OnSerialize(json& outJson) final
	{
		outJson["ModelPath"] = ModelPath.LocalPath;
	}

	virtual void OnDeserialize(const json& inJson) final
	{
		ModelPath = Path(inJson["ModelPath"]);
	}
#if ME_EDITOR

	virtual void OnEditorInspect() final
	{
		ImGui::Text("Path:");
		ImGui::SameLine();
		ImGui::Text(ModelPath.LocalPath.c_str());

		if (!ModelHandle)
		{
			static std::vector<Path> Models;
			Path path = Path("Assets");
			if (Models.empty())
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(path.FullPath))
				{
					Path filePath(entry.path().string());
					if ((filePath.LocalPath.rfind(".fbx") != std::string::npos || filePath.LocalPath.rfind(".obj") != std::string::npos)
						&& filePath.LocalPath.rfind(".meta") == std::string::npos)
					{
						Models.push_back(filePath);
					}
				}
			}

			if (ImGui::BeginCombo("##Model", "Select a model to construct"))
			{
				for (size_t n = 0; n < Models.size(); n++)
				{
					if (ImGui::Selectable(Models[n].LocalPath.c_str(), false))
					{
						ModelPath = Models[n];
						Models.clear();
						Init();
						break;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::Text("Select Model");
		}

		if (ModelShader)
		{
		}
	}
#endif
};

ME_REGISTER_COMPONENT_FOLDER(Model, "Rendering")