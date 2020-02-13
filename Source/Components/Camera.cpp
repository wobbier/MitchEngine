#include "PCH.h"
#include "Camera.h"
#include "Graphics/SkyBox.h"
#include "Graphics/Texture.h"
#include "Math/Frustrum.h"

Camera* Camera::CurrentCamera = nullptr;
Camera* Camera::EditorCamera = nullptr;

Camera::Camera()
	: Component("Camera")
{
	Front = Vector3(0.f, 0.f, -1.f);
	CameraFrustum = new Frustum();
}

Camera::~Camera()
{
	if (CurrentCamera == this)
	{
		CurrentCamera = nullptr;
	}
	delete CameraFrustum;
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

const int Camera::GetCameraId() const
{
	return m_id;
}

const bool Camera::IsMain() const
{
	return (Camera::CurrentCamera == this);
}

void Camera::Deserialize(const json& inJson)
{
	if (inJson.contains("Skybox"))
	{
		Skybox = new Moonlight::SkyBox(inJson["Skybox"]);
	}
}

void Camera::Serialize(json& outJson)
{
	Component::Serialize(outJson);

	outJson["Zoom"] = Zoom;
	outJson["IsCurrent"] = IsCurrent();

	if (Skybox)
	{
		outJson["Skybox"] = Skybox->SkyMaterial->GetTexture(Moonlight::TextureType::Diffuse)->GetPath().LocalPath;
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

	ImGui::SliderFloat("Near", &Near, 0.1f, 200.0f);
	ImGui::SliderFloat("Far", &Far, 0.2f, 2000.0f);

	if (ImGui::BeginCombo("##ClearType", (ClearType == Moonlight::ClearColorType::Color) ? "Color" : "Skybox"))
	{
		if (ImGui::Selectable("Color", (ClearType == Moonlight::ClearColorType::Color)))
		{
			ClearType = Moonlight::ClearColorType::Color;
		}
		if (ImGui::Selectable("Skybox", (ClearType == Moonlight::ClearColorType::Skybox)))
		{
			ClearType = Moonlight::ClearColorType::Skybox;
		}
		ImGui::EndCombo();
	}

	if(ClearType == Moonlight::ClearColorType::Skybox)
	{
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
		if (Skybox)
		{
			const Moonlight::Texture* texture = Skybox->SkyMaterial->GetTexture(Moonlight::TextureType::Diffuse);
			ImGui::ImageButton(((texture) ? (void*)texture->ShaderResourceView : nullptr), ImVec2(30, 30));
		}
		else
		{
			ImGui::ImageButton(nullptr, ImVec2(30, 30));
		}
		ImGui::SameLine();
		ImGui::Text("Skybox Texture");
	}
	else if (ClearType == Moonlight::ClearColorType::Color)
	{
		ImGui::ColorEdit3("Clear Color", &ClearColor[0]);
	}
}

#endif