#include "PCH.h"
#include "Mesh.h"

#include "Graphics/MeshData.h"
#include "Graphics/Texture.h"
#include "Utils/HavanaUtils.h"
#include "imgui.h"

Mesh::Mesh()
	: Component("Mesh")
{

}

Mesh::Mesh(Moonlight::MeshType InType, Moonlight::Material* InMaterial)
	: Component("Mesh")
	, Type(InType)
	, MeshMaterial(InMaterial)
{
	if (InMaterial == nullptr)
	{
		//fuck load some other shit, dude
	}
}

Mesh::Mesh(Moonlight::MeshData* mesh)
	: Component("Mesh")
	, MeshReferece(mesh)
	, Type(Moonlight::MeshType::Model)
{
	MeshMaterial = new Moonlight::Material(*MeshReferece->material);
}

void Mesh::Init()
{

}

unsigned int Mesh::GetId()
{
	return Id;
}

Moonlight::MeshType Mesh::GetType() const
{
	return Type;
}

void Mesh::Serialize(json& outJson)
{
	Component::Serialize(outJson);
	if (MeshMaterial)
	{
		MeshMaterial->OnSerialize(outJson);
	}
	outJson["MeshType"] = GetMeshTypeString(Type);
}

void Mesh::Deserialize(const json& inJson)
{
	if (!MeshMaterial)
	{
		MeshMaterial = new Moonlight::Material();
	}
	MeshMaterial->OnDeserialize(inJson);

	if (inJson.contains("MeshType"))
	{
		Type = GetMeshTypeFromString(inJson["MeshType"]);
	}
}

std::string Mesh::GetMeshTypeString(Moonlight::MeshType InType)
{
	switch (InType)
	{
	case Moonlight::MeshType::Plane:
		return "Plane";
	case Moonlight::MeshType::Model:
	default:
		return "Model";
	}
}

Moonlight::MeshType Mesh::GetMeshTypeFromString(const std::string& InType)
{
	if (InType == "Plane")
	{
		return Moonlight::MeshType::Plane;
	}
	else
	{
		return Moonlight::MeshType::Model;
	}
}

#if ME_EDITOR

void Mesh::OnEditorInspect()
{
	if (MeshShader && MeshMaterial)
	{
		bool transparent = MeshMaterial->IsTransparent();
		ImGui::Checkbox("Render Transparent", &transparent);
		if (transparent)
		{
			MeshMaterial->SetRenderMode(Moonlight::RenderingMode::Transparent);
		}
		else
		{
			MeshMaterial->SetRenderMode(Moonlight::RenderingMode::Opaque);
		}
		HavanaUtils::EditableVector("Tiling", MeshMaterial->Tiling);
		ImGui::Text("Vertices: %i", MeshReferece->vertices.size());
		if (ImGui::TreeNode("Material"))
		{
			static std::vector<Path> Textures;
			Path path = Path("Assets");
			if (Textures.empty())
			{
				Textures.push_back(Path(""));
				for (const auto& entry : std::filesystem::recursive_directory_iterator(path.FullPath))
				{
					Path filePath(entry.path().string());
					if ((filePath.LocalPath.rfind(".png") != std::string::npos || filePath.LocalPath.rfind(".jpg") != std::string::npos || filePath.LocalPath.rfind(".tif") != std::string::npos)
						&& filePath.LocalPath.rfind(".meta") == std::string::npos)
					{
						Textures.push_back(filePath);
					}
				}
			}

			ImGui::ColorEdit3("Diffuse Color", &MeshMaterial->DiffuseColor[0]);

			int i = 0;
			for (auto texture : MeshMaterial->GetTextures())
			{
				std::string label("##Texture" + std::to_string(i));
				{
					ImGui::ImageButton(((texture) ? (void*)texture->ShaderResourceView : nullptr), ImVec2(30, 30));
					ImGui::SameLine();
				}
				if (ImGui::BeginCombo(label.c_str(), ((texture) ? texture->GetPath().LocalPath.c_str() : "")))
				{
					for (int n = 0; n < Textures.size(); n++)
					{
						if (ImGui::Selectable(Textures[n].LocalPath.c_str(), false))
						{
							if (!Textures[n].LocalPath.empty())
							{
								MeshMaterial->SetTexture(static_cast<Moonlight::TextureType>(i), ResourceCache::GetInstance().Get<Moonlight::Texture>(Textures[n]));
							}
							else
							{
								MeshMaterial->SetTexture(static_cast<Moonlight::TextureType>(i), nullptr);
							}
							Textures.clear();
							break;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				ImGui::Text(Moonlight::Texture::ToString(static_cast<Moonlight::TextureType>(i)).c_str());
				if (texture)
				{
					if (i == static_cast<int>(Moonlight::TextureType::Diffuse))
					{
					}
				}
				i++;
			}
			ImGui::TreePop();
		}
	}
}

#endif