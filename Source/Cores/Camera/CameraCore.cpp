#include "PCH.h"
#include "CameraCore.h"
#include "Components/Camera.h"

CameraCore::CameraCore() : Base(ComponentFilter().Requires<Camera>().Requires<Transform>())
{
}

CameraCore::~CameraCore()
{
}

void CameraCore::Init()
{
	WeakPtr<Entity> CameraEntity = GetWorld().CreateEntity();
	DefaultCamera = &(CameraEntity.lock()->AddComponent<Camera>());
}

void CameraCore::Update(float dt)
{
	BROFILER_CATEGORY("CameraCore::Update", Brofiler::Color::CornflowerBlue);
	auto Cameras = GetEntities();

	for (auto& InEntity : Cameras)
	{
		// Get our components that our Core required
		Camera& CameraComponent = InEntity.GetComponent<Camera>();
		Transform& TransformComponent = InEntity.GetComponent<Transform>();

		if(CameraComponent.IsCurrent())
		{
			CameraComponent.UpdateCameraTransform(TransformComponent.GetPosition());
		}
	}
}

void CameraCore::OnEntityAdded(Entity& NewEntity)
{
	if (Camera::CurrentCamera == DefaultCamera)
	{
		NewEntity.GetComponent<Camera>().SetCurrent();
	}
}
