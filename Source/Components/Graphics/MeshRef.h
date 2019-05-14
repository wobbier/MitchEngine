#pragma once
#include "ECS/Component.h"
#include "Dementia.h"
#include "Graphics/Mesh.h"

class MeshRef
	: public Component<MeshRef>
{
public:
	MeshRef()
	{
	}
	MeshRef(Moonlight::Mesh* mesh)
		: MeshReferece(mesh)
	{
	}

	// Separate init from construction code.
	virtual void Init() final
	{
	};

	virtual void Deserialize(const json& inJson) final
	{
	}
	unsigned int Id = 0;
	unsigned int GetId() {
		return Id;
	}

	Moonlight::Mesh* MeshReferece;
	Moonlight::Shader* MeshShader;
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

	}

	virtual void Serialize(json& outJson) final
	{
		Component::Serialize(outJson);

	}
#endif
};

ME_REGISTER_COMPONENT(MeshRef)