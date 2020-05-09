#include "PCH.h"
#include "Mesh.h"

#include "Graphics/MeshData.h"
#include "Graphics/Texture.h"

#if ME_EDITOR

#include "Utils/HavanaUtils.h"
#include "imgui.h"
#include "HavanaEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Cores/Rendering/RenderCore.h"

#endif
#include "RenderCommands.h"

Mesh::Mesh()
	: Component("Mesh")
{

}

Mesh::Mesh(Moonlight::MeshType InType, Moonlight::Material* InMaterial, Moonlight::ShaderCommand* InShader)
	: Component("Mesh")
	, Type(InType)
	, MeshMaterial(InMaterial)
	, MeshShader(InShader)
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

void Mesh::OnSerialize(json& outJson)
{
	if (MeshMaterial)
	{
		MeshMaterial->OnSerialize(outJson);
	}
	outJson["MeshType"] = GetMeshTypeString(Type);
}

void Mesh::OnDeserialize(const json& inJson)
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
	if (!MeshReferece)
	{
		if (ImGui::BeginCombo("Mesh Type", GetMeshTypeString(Type).c_str()))
		{
			for (int n = 0; n < Moonlight::MeshType::MeshCount; n++)
			{
				if (ImGui::Selectable(GetMeshTypeString((Moonlight::MeshType)n).c_str(), false))
				{
					Type = (Moonlight::MeshType)n;

					static_cast<RenderCore*>(GetEngine().GetWorld().lock()->GetCore(RenderCore::GetTypeId()))->UpdateMesh(this);
					
					break;
				}
			}
			ImGui::EndCombo();
		}
	}
	if (MeshShader && MeshMaterial)
	{
		bool transparent = MeshMaterial->IsTransparent();
		if (MeshReferece)
		{
			ImGui::Text("Vertices: %i", MeshReferece->vertices.size());
		}
		if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
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
					if (ImGui::ImageButton(((texture) ? (void*)texture->ShaderResourceView : nullptr), ImVec2(30, 30)))
					{
						PreviewResourceEvent evt;
						evt.Subject = texture;
						evt.Fire();
					}
// 					if (ImGui::BeginPopupModal("ViewTexture", &ViewTexture, ImGuiWindowFlags_MenuBar))
// 					{
// 						if (texture && texture->ShaderResourceView)
// 						{
// 							// Get the current cursor position (where your window is)
// 							ImVec2 pos = ImGui::GetCursorScreenPos();
// 							ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
// 							Vector2 RenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
// 
// 							// Ask ImGui to draw it as an image:
// 							// Under OpenGL the ImGUI image type is GLuint
// 							// So make sure to use "(void *)tex" but not "&tex"
// 							ImGui::GetWindowDrawList()->AddImage(
// 								(void*)texture->ShaderResourceView,
// 								ImVec2(pos.x, pos.y),
// 								ImVec2(maxPos));
// 							//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));
// 
// 						}
// 						if (ImGui::Button("Close"))
// 						{
// 							ViewTexture = false;
// 							ImGui::CloseCurrentPopup();
// 						}
// 						ImGui::EndPopup();
// 					}
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