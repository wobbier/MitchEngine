#include "PCH.h"
#include "CameraCore.h"
#include "Components/Camera.h"
#include "Engine/Engine.h"
#include "Window/IWindow.h"
#include "Math/Frustrum.h"
#include "Camera/CameraData.h"
#include "BGFXRenderer.h"

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

		Moonlight::CameraData* CamData = GetEngine().GetRenderer().GetCameraCache().Get(CameraComponent.m_id);
		if (CamData)
		{
			CamData->Position = TransformComponent.GetWorldPosition();
			CamData->Front = TransformComponent.Front();
			CamData->Up = TransformComponent.Up();
			CamData->OutputSize = CameraComponent.OutputSize;
			CamData->FOV = CameraComponent.GetFOV();
			CamData->Near = CameraComponent.Near;
			CamData->Far = CameraComponent.Far;
			CamData->Skybox = CameraComponent.Skybox;
			CamData->ClearColor = CameraComponent.ClearColor;
			CamData->ClearType = CameraComponent.ClearType;
			CamData->Projection = CameraComponent.Projection;
			CamData->OrthographicSize = CameraComponent.OrthographicSize;
			CamData->IsMain = CameraComponent.IsMain();
			//CamData.CameraFrustum = CameraComponent.CameraFrustum;
			CamData->UITexture = BGFX_INVALID_HANDLE;

			GetEngine().GetRenderer().GetCameraCache().Update(CameraComponent.m_id, *CamData);
		}
	}
}

void CameraCore::OnEntityAdded(Entity& NewEntity)
{
	if (Camera::CurrentCamera == DefaultCamera)
	{
		NewEntity.GetComponent<Camera>().SetCurrent();
	}
	auto& renderer = GetEngine().GetRenderer();
	Moonlight::CameraData camData = CreateCameraData(NewEntity.GetComponent<Transform>(), NewEntity.GetComponent<Camera>());
	unsigned int id = renderer.GetCameraCache().Push(camData);
	NewEntity.GetComponent<Camera>().m_id = id;
	renderer.RecreateFrameBuffer(id);
}

Moonlight::CameraData CameraCore::CreateCameraData(Transform& InTransform, Camera& InCamera)
{
	Moonlight::CameraData CamData;

	CamData.Position = InTransform.GetWorldPosition();
	CamData.Front = InTransform.Front();
	CamData.Up = InTransform.Up();
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

	//Vector3 Right = CamData.Front.Cross(Vector3::Up).Normalized();
	//Vector3 Up = CamData.Front.Cross(Right).Normalized();

	//CamData.CameraFrustum = InCamera.CameraFrustum;
	CamData.Buffer = new Moonlight::FrameBuffer(static_cast<uint32_t>(CamData.OutputSize.x), static_cast<uint32_t>(CamData.OutputSize.y));

	return CamData;
}

void CameraCore::OnEntityRemoved(Entity& InEntity)
{
	unsigned int id = InEntity.GetComponent<Camera>().m_id;
	Moonlight::CameraData* camData = GetEngine().GetRenderer().GetCameraCache().Get(id);
	delete camData->Buffer;
	GetEngine().GetRenderer().GetCameraCache().Pop(id);
}
