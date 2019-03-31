#pragma once
#include <gtc/matrix_transform.hpp>

#include "ECS/Component.h"
#include "Components/Transform.h"
#include "Math/Vector3.h"
#include "Math/Matirx4.h"
#include "Dementia.h"

class Camera
	: public Component<Camera>
{
public:
	static Camera* CurrentCamera;

	Vector3 Position;
	Vector3 Front;
	Vector3 Up;
	float Zoom = 45.0f;
	float Yaw = -90.f;
	float Pitch = 0.f;
	float Roll = 0.f;

	Camera();
	~Camera() = default;

	virtual void Init() override;

	Matrix4 GetViewMatrix();
	void UpdateCameraTransform(Vector3 TransformComponent);
	bool IsCurrent();
	void SetCurrent();
	float GetFOV();
private:
	float m_FOV = 45.f;

#if ME_EDITOR
	virtual void OnEditorInspect() final
	{
		Havana::Text("Front", Front);
	}
#endif
};
