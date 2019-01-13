#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/Shader.h"
#include "Device/IDevice.h"
#include "Renderer.h"

class RenderCore
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

	bool Render();
	Cubemap* SkyboxMap = nullptr;
	Moonlight::Shader* SkyboxShader = nullptr;

	virtual void OnDeviceLost() override;
	virtual void OnDeviceRestored() override;

private:
#if ME_PLATFORM_WIN64
	unsigned int skyboxVAO, skyboxVBO;
#endif
	Moonlight::Renderer* m_renderer;
};
