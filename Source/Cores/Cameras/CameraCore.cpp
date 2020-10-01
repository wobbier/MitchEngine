#include "PCH.h"
#include "CameraCore.h"
#include "Components/Camera.h"
#include "Engine/Engine.h"
#include "Window/IWindow.h"
#include "Math/Frustrum.h"

CameraCore::CameraCore() : Base(ComponentFilter().Requires<Camera>().Requires<Transform>())
{
	SetIsSerializable(false);
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

#if ME_EDITOR
#else
		if (CameraComponent.IsMain())
		{
			CameraComponent.OutputSize = GetEngine().GetWindow()->GetSize();
		}
#endif

		Moonlight::CameraData& CamData = GetEngine().GetRenderer().GetCamera(CameraComponent.m_id);
		CamData.Position = TransformComponent.GetWorldPosition();
		CamData.Front = TransformComponent.Front();
		CamData.Up = Vector3::Up;
		CamData.OutputSize = CameraComponent.OutputSize;
		CamData.FOV = CameraComponent.GetFOV();
		CamData.Near = CameraComponent.Near;
		CamData.Far = CameraComponent.Far;
		CamData.Skybox = CameraComponent.Skybox;
		CamData.ClearColor = CameraComponent.ClearColor;
		CamData.ClearType = CameraComponent.ClearType;
		CamData.Projection = CameraComponent.Projection;
		CamData.OrthographicSize = CameraComponent.OrthographicSize;
		CamData.IsMain = CameraComponent.IsMain();
		CamData.CameraFrustum = CameraComponent.CameraFrustum;

		GetEngine().GetRenderer().UpdateCamera(CameraComponent.m_id, CamData);
	}
}

void CameraCore::OnEntityAdded(Entity& NewEntity)
{
	if (Camera::CurrentCamera == DefaultCamera)
	{
		NewEntity.GetComponent<Camera>().SetCurrent();
	}
	NewEntity.GetComponent<Camera>().m_id = GetEngine().GetRenderer().PushCamera(CreateCameraData(NewEntity.GetComponent<Transform>(), NewEntity.GetComponent<Camera>()));
}

Moonlight::CameraData CameraCore::CreateCameraData(Transform& InTransform, Camera& InCamera)
{
	Moonlight::CameraData CamData;

	CamData.Position = InTransform.GetWorldPosition();
	CamData.Front = InTransform.Front();
	CamData.Up = Vector3::Up;
	if (InCamera.OutputSize.IsZero())
	{
		InCamera.OutputSize = GetEngine().GetWindow()->GetSize();
	}
	CamData.OutputSize = InCamera.OutputSize;
	CamData.FOV = InCamera.GetFOV();
	CamData.Near = InCamera.Near;
	CamData.Far = InCamera.Far;
	CamData.Skybox = InCamera.Skybox;
	CamData.ClearColor = InCamera.ClearColor;
	CamData.ClearType = InCamera.ClearType;
	CamData.Projection = InCamera.Projection;
	CamData.OrthographicSize = InCamera.OrthographicSize;
	CamData.IsMain = InCamera.IsMain();

	Vector3 Right = CamData.Front.Cross(Vector3::Up).Normalized();
	Vector3 Up = CamData.Front.Cross(Right).Normalized();

	CamData.CameraFrustum = InCamera.CameraFrustum;

	return CamData;
}

void CameraCore::OnEntityRemoved(Entity& InEntity)
{
	GetEngine().GetRenderer().PopCamera(InEntity.GetComponent<Camera>().m_id);
}
