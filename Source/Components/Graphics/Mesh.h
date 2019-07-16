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