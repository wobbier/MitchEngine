#pragma once
#include "ECS/Component.h"
#include "Dementia.h"
#include "Graphics/MeshData.h"

class Mesh
	: public Component<Mesh>
{
public:
	Mesh()
	{
	}
	Mesh(Moonlight::MeshData* mesh)
		: MeshReferece(mesh)
	{
	}

	// Separate init from construction code.
	virtual void Init() final
	{
	};

	virtual void Deserialize(const json& inJson) final
	{
		if (inJson.contains("test"))
		{
			Test = (bool)inJson["test"];
		}
	}
	unsigned int Id = 0;
	unsigned int GetId() {
		return Id;
	}

	Moonlight::MeshData* MeshReferece = nullptr;
	Moonlight::Shader* MeshShader = nullptr;
private:
#if ME_EDITOR
	virtual void OnEditorInspect() final
	{
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
		ImGui::Checkbox("test", &Test);
	}

	virtual void Serialize(json& outJson) final
	{
		Component::Serialize(outJson);

		outJson["test"] = true;
	}
	bool Test = false;
#endif
};

ME_REGISTER_COMPONENT(Mesh)