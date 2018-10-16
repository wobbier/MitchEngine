#pragma once

#include "Graphics\Common\StepTimer.h"
#include "Graphics\Common\DeviceResources.h"
#include "Graphics\Content\Sample3DSceneRenderer.h"
#include "Graphics\Content\SampleFpsTextRenderer.h"

// Renders Direct2D and 3D content on the screen.
class TestDirectX : public DX::IDeviceNotify
{
public:
	TestDirectX(const std::shared_ptr<DX::DeviceResources>& deviceResources);
	~TestDirectX();
	void CreateWindowSizeDependentResources();
	void Update();
	bool Render();

	// IDeviceNotify
	virtual void OnDeviceLost();
	virtual void OnDeviceRestored();

private:
	// Cached pointer to device resources.
	std::shared_ptr<DX::DeviceResources> m_deviceResources;

	// TODO: Replace with your own content renderers.
	std::unique_ptr<Sample3DSceneRenderer> m_sceneRenderer;
	std::unique_ptr<SampleFpsTextRenderer> m_fpsTextRenderer;

	// Rendering loop timer.
	DX::StepTimer m_timer;
};
