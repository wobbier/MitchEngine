#include "PCH.h"
#include "CameraCore.h"
#include "Components/Camera.h"
#include "Engine/Engine.h"
#include "Window/IWindow.h"
#include "Math/Frustrum.h"
#include "Camera/CameraData.h"
#include "Renderer.h"
#include "Mathf.h"

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

void CameraCore::Update(const UpdateContext& inUpdateContext)
{
}

void CameraCore::LateUpdate(const UpdateContext& inUpdateContext)
{
	OPTICK_CATEGORY("CameraCore::Update", Optick::Category::Camera);
	auto Cameras = GetEntities();

	for (auto& InEntity : Cameras)
	{
		// Get our components that our Core required
		Camera& CameraComponent = InEntity.GetComponent<Camera>();
		Transform& TransformComponent = InEntity.GetComponent<Transform>();

#if USING( ME_EDITOR )
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
			CamData->OutputSize = { Mathf::Abs(CameraComponent.OutputSize.x), Mathf::Abs(CameraComponent.OutputSize.y) };
			
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

			Vector3 eye = { CamData->Position.x, CamData->Position.y, CamData->Position.z };
			Vector3 at = { CamData->Position.x + CamData->Front.x, CamData->Position.y + CamData->Front.y, CamData->Position.z + CamData->Front.z };
			Vector3 up = { CamData->Up.x, CamData->Up.y, CamData->Up.z };

			bx::mtxProj(&CameraComponent.WorldToCamera.GetInternalMatrix()[0][0], CameraComponent.GetFOV(), float(CameraComponent.OutputSize.x) / float(CameraComponent.OutputSize.y), std::max(CameraComponent.Near, 1.f), CameraComponent.Far, bgfx::getCaps()->homogeneousDepth);
			CamData->IsOblique = CameraComponent.isOblique;
			CamData->ObliqueData = CameraComponent.ObliqueMatData;
			CamData->View = glm::lookAtLH(eye.InternalVector, at.InternalVector, up.InternalVector);
			CamData->ProjectionMatrix = CameraComponent.WorldToCamera;

			CameraComponent.WorldToCamera = CamData->View;// CamData->ProjectionMatrix.GetInternalMatrix()* CamData->View.GetInternalMatrix();
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
