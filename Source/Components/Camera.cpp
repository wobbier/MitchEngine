#include "PCH.h"
#include "Camera.h"
#include "Graphics/SkyBox.h"
#include "Graphics/Texture.h"

Camera* Camera::CurrentCamera = nullptr;
Camera* Camera::EditorCamera = nullptr;

Camera::Camera()
{
	Position = Vector3(0.f, 0.f, 2.f);
	Up = Vector3(0.f, 1.f, 0.f);
	Front = Vector3(0.f, 0.f, -1.f);
}

Camera::~Camera()
{
	if (CurrentCamera == this)
	{
		CurrentCamera = nullptr;
	}
}

void Camera::Init()
{
	if (!CurrentCamera)
	{
		CurrentCamera = this;
	}
}

Matrix4 Camera::GetViewMatrix()
{
	return Matrix4();//glm::lookAt(Position.GetInternalVec(), Position.GetInternalVec() + Front.GetInternalVec(), Up.GetInternalVec()));
}

void Camera::LookAt(const Vector3& TargetPosition)
{
	Front = (TargetPosition - Position).Normalized();
}

void Camera::UpdateCameraTransform(Vector3 NewPosition)
{
	Position = NewPosition;
}

bool Camera::IsCurrent()
{
	return Camera::CurrentCamera == this;
}

void Camera::SetCurrent()
{
	Camera::CurrentCamera = this;
}

float Camera::GetFOV()
{
	return m_FOV;
}

void Camera::Deserialize(const json& inJson)
{
	if (inJson.contains("Skybox"))
	{
		Skybox = new Moonlight::SkyBox(inJson["Skybox"]);
	}
}

#if ME_EDITOR

void Camera::OnEditorInspect()
{
	HavanaUtils::Text("Front", Front);
	if (ImGui::Button("Set Current"))
	{
		SetCurrent();
	}

	if (ImGui::BeginCombo("##Projection", (Projection == Moonlight::ProjectionType::Perspective) ? "Perspective" : "Orthographic"))
	{
		if (ImGui::Selectable("Perspective", (Projection == Moonlight::ProjectionType::Perspective)))
		{
			Projection = Moonlight::ProjectionType::Perspective;
		}
		if (ImGui::Selectable("Orthographic", (Projection == Moonlight::ProjectionType::Orthographic)))
		{
			Projection = Moonlight::ProjectionType::Orthographic;
		}
		ImGui::EndCombo();
	}

	if (Projection == Moonlight::ProjectionType::Perspective)
	{
		ImGui::SliderFloat("Field of View", &m_FOV, 1.0f, 200.0f);
	}
	else if (Projection == Moonlight::ProjectionType::Orthographic)
	{
		ImGui::SliderFloat("Size", &OrthographicSize, 0.1f, 200.0f);
	}

	static std::vector<Path> Textures;
	Path path = Path("Assets");
	if (Textures.empty())
	{
		Textures.push_back(Path(""));
		for (const auto& entry : std::filesystem::recursive_directory_iterator(path.FullPath))
		{
			Path filePath(entry.path().string());
			if ((filePath.LocalPath.rfind(".png") != std::string::npos || filePath.LocalPath.rfind(".jpg") != std::string::npos)
				&& filePath.LocalPath.rfind(".meta") == std::string::npos)
			{
				Textures.push_back(filePath);
			}
		}
	}

	{
		if (ImGui::BeginCombo("##SkyboxTexture", ""))
		{
			for (int n = 0; n < Textures.size(); n++)
			{
				if (ImGui::Selectable(Textures[n].LocalPath.c_str(), false))
				{
					if (Textures[n].LocalPath.empty())
					{
						if (Skybox)
						{
							delete Skybox;
							Skybox = nullptr;
						}
					}
					else
					{
						if (Skybox)
						{
							Skybox->SkyMaterial->SetTexture(Moonlight::TextureType::Diffuse, ResourceCache::GetInstance().Get<Moonlight::Texture>(Textures[n]));
						}
						else
						{
							Skybox = new Moonlight::SkyBox(Textures[n].LocalPath);
						}
					}
					Textures.clear();
					break;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::ImageButton(((Skybox) ? (void*)Skybox->SkyMaterial->GetTexture(Moonlight::TextureType::Diffuse)->ShaderResourceView : nullptr), ImVec2(30, 30));
		ImGui::SameLine();
		ImGui::Text("Skybox Texture");
	}
}

#endif