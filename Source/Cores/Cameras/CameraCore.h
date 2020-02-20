#pragma once
#include "ECS/Core.h"
#include "Camera/CameraData.h"
#include "Components/Camera.h"

class CameraCore
	: public Core<CameraCore>
{
public:
	CameraCore();
	~CameraCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;

	Moonlight::CameraData CreateCameraData(Transform& InTransform, Camera& InCamera);

	void OnEntityRemoved(Entity& InEntity) override;

private:
	class Camera* DefaultCamera;
};
