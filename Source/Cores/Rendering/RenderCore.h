#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/Shader.h"
#include "Device/IDevice.h"
#include "Renderer.h"
#include "Graphics/ModelResource.h"

class RenderCore final
	: public Core<RenderCore>
	, public Moonlight::IDeviceNotify
{
public:
	RenderCore();
	~RenderCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;
	virtual void OnEntityRemoved(Entity& InEntity) final;

	Cubemap* SkyboxMap = nullptr;
	Moonlight::Shader* SkyboxShader = nullptr;

	virtual void OnDeviceLost() override;
	virtual void OnDeviceRestored() override;

private:
	
	Moonlight::Renderer* m_renderer;
	std::shared_ptr<ModelResource> cube;
	Moonlight::Shader* shader;
};
