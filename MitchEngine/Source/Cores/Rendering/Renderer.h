#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/Shader.h"

#include "Graphics\Common\StepTimer.h"
#include "Graphics\Common\DeviceResources.h"
#include "Graphics\Content\TestModelRenderer.h"
#include "Graphics\Content\SampleFpsTextRenderer.h"

class Renderer
	: public Core<Renderer>
	, public DX::IDeviceNotify
{
public:
	Renderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
	~Renderer();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	bool Render();
	Cubemap* SkyboxMap = nullptr;
	Shader* SkyboxShader = nullptr;

	void CreateWindowSizeDependentResources();
	virtual void OnDeviceLost() override;
	virtual void OnDeviceRestored() override;

private:
	std::shared_ptr<DX::DeviceResources> m_deviceResources;
	std::unique_ptr<TestModelRenderer> m_sceneRenderer;
	std::unique_ptr<SampleFpsTextRenderer> m_fpsTextRenderer;

	// Rendering loop timer.
	DX::StepTimer m_timer;
};
