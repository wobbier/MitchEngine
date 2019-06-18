#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <string>
#include "imgui.h"
#include "Graphics/ShaderCommand.h"
#include "Path.h"
#include <filesystem>

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

	virtual void Deserialize(const json& inJson) final {
		ModelPath = Path(inJson["ModelPath"]);
	}
private:
	Path ModelPath;
	bool IsInitialized = false;
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

			struct FuncHolder {
				static bool ItemGetter(void* data, int idx, const char** out_str) {
					std::vector<Path>* data2 = static_cast<std::vector<Path>*>(data);
					
					*out_str = &*(data2->at(idx).LocalPath.c_str());

					return true;
				}
			};
			static int selected_item = 0;
			if (ImGui::Combo("Model", &selected_item, &FuncHolder::ItemGetter, &Models, Models.size()))
			{
				ModelPath = Models.at(selected_item);
				Models.clear();
				Init();
			}
		}
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