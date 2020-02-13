#pragma once

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

class Frustum;

class Camera
	: public Component<Camera>
{
	friend class CameraCore;
public:
	static Camera* CurrentCamera;
	static Camera* EditorCamera;

	Vector3 Front;
	Vector3 ClearColor;
	Vector2 OutputSize;
	float Zoom = 45.0f;
	float Yaw = -90.f;
	float Pitch = 0.f;
	float Roll = 0.f;
	float OrthographicSize = 50.f;
	float Near = .1f;
	float Far = 100.f;

	Camera();
	~Camera();

	virtual void Init() override;

	Matrix4 GetViewMatrix();
	bool IsCurrent();
	void SetCurrent();
	float GetFOV();

	const int GetCameraId() const;

	const bool IsMain() const;

	Frustum* CameraFrustum = nullptr;
	Moonlight::SkyBox* Skybox = nullptr;
	Moonlight::ProjectionType Projection = Moonlight::ProjectionType::Perspective;
	Moonlight::ClearColorType ClearType = Moonlight::ClearColorType::Color;

#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif

private:
	float m_FOV = 45.f;
	unsigned int m_id = 0;
	virtual void Deserialize(const json& inJson) override;
	virtual void Serialize(json& outJson) final;
};
ME_REGISTER_COMPONENT(Camera)
