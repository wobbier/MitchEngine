#include "PCH.h"
#include "CameraCore.h"
#include "Components/Camera.h"

CameraCore::CameraCore() : Base(ComponentFilter().Requires<Camera>().Requires<Transform>())
{
	DefaultCamera = new Camera();
}

CameraCore::~CameraCore()
{
	delete DefaultCamera;
}

void CameraCore::Init()
{
	DefaultCamera->SetCurrent();
}

void CameraCore::Update(float dt)
{
	OPTICK_CATEGORY("CameraCore::Update", Optick::Category::Camera);
	auto Cameras = GetEntities();

	for (auto& InEntity : Cameras)
	{
		// Get our components that our Core required
		Camera& CameraComponent = InEntity.GetComponent<Camera>();
		Transform& TransformComponent = InEntity.GetComponent<Transform>();

		CameraComponent.UpdateCameraTransform(TransformComponent.GetPosition());
	}
}

void CameraCore::OnEntityAdded(Entity& NewEntity)
{
	if (Camera::CurrentCamera == DefaultCamera)
	{
		NewEntity.GetComponent<Camera>().SetCurrent();
	}
}
