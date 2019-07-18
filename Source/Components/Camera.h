#pragma once
#include <gtc/matrix_transform.hpp>

#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Components/Transform.h"
#include "Math/Vector3.h"
#include "Math/Matirx4.h"
#include "Dementia.h"
#include <imgui.h>
#include "Utils/HavanaUtils.h"
#include "Graphics/SkyBox.h"
#include "Camera/CameraData.h"

class Camera
	: public Component<Camera>
{
public:
	static Camera* CurrentCamera;
	static Camera* EditorCamera;

	Vector3 Position;
	Vector3 Front;
	Vector3 Up;
	float Zoom = 45.0f;
	float Yaw = -90.f;
	float Pitch = 0.f;
	float Roll = 0.f;
	float OrthographicSize = 1.f;

	Camera();
	~Camera();

	virtual void Init() override;

	Matrix4 GetViewMatrix();
	void UpdateCameraTransform(Vector3 TransformComponent);
	bool IsCurrent();
	void SetCurrent();
	float GetFOV();

	Moonlight::SkyBox* Skybox = nullptr;
	Moonlight::ProjectionType Projection = Moonlight::ProjectionType::Perspective;

#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif

private:
	float m_FOV = 45.f;

	virtual void Deserialize(const json& inJson) override;
	virtual void Serialize(json& outJson) final
	{
		Component::Serialize(outJson);

		outJson["Zoom"] = Zoom;
		outJson["IsCurrent"] = IsCurrent();

		if (Skybox)
		{
			outJson["Skybox"] = Skybox->SkyMaterial->GetTexture(Moonlight::TextureType::Diffuse)->GetPath().LocalPath;
		}
	}
};
ME_REGISTER_COMPONENT(Camera)
