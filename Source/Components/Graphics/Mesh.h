#pragma once
#include "ECS/Component.h"
#include "Dementia.h"
#include "Graphics/MeshData.h"
#include "Graphics/Texture.h"

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

	unsigned int Id = 0;
	unsigned int GetId() {
		return Id;
	}

	Moonlight::MeshData* MeshReferece = nullptr;
	Moonlight::ShaderCommand* MeshShader = nullptr;
	Moonlight::Material* MeshMaterial = nullptr;
private:
#if ME_EDITOR
	virtual void Deserialize(const json& inJson) final
	{
		if (inJson.contains("test"))
		{
			Test = (bool)inJson["test"];
		}
		if (!MeshMaterial)
		{
			MeshMaterial = new Moonlight::Material();
		}
		MeshMaterial->OnDeserialize(inJson);
	}
	virtual void OnEditorInspect() final
	{
		if (MeshShader)
		{
			ImGui::Text("Vertices: %i", MeshReferece->vertices.size());
			if (ImGui::TreeNode("Material"))
			{
				for (auto texture : MeshMaterial->GetTextures())
				{
					if (texture != nullptr)
					{
						ImGui::Text("Texture Path: %s", texture->GetPath().LocalPath.c_str());

						ImGui::ImageButton((void*)texture->CubesTexture, ImVec2(30, 30));
						ImGui::SameLine();
						ImGui::Text("Texture Type: %s", Moonlight::Texture::ToString(texture->Type).c_str());
					}
				}
				ImGui::TreePop();
			}
		}
	}

	virtual void Serialize(json& outJson) final
	{
		Component::Serialize(outJson);
		if (MeshMaterial)
		{
			MeshMaterial->OnSerialize(outJson);
		}
		outJson["test"] = true;
	}
	bool Test = false;
#endif
};

ME_REGISTER_COMPONENT(Mesh)