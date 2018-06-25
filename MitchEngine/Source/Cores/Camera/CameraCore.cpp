#include "CameraCore.h"
#include "Components/Camera.h"

CameraCore::CameraCore() : Base(ComponentFilter().Requires<Camera>())
{
}

CameraCore::~CameraCore()
{
}

void CameraCore::Init()
{
}

void CameraCore::Update(float dt)
{
	auto Cameras = GetEntities();
	for (auto& InEntity : Cameras)
	{
		// Get our components that our Core required
		Camera& CameraComponent = InEntity.GetComponent<Camera>();
		Transform& TransformComponent = InEntity.GetComponent<Transform>();

		if (CameraComponent.IsCurrent())
		{
			CameraComponent.UpdateCameraTransform(TransformComponent.Position);
		}
	}
}