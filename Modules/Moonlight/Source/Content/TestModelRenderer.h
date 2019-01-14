#pragma once
#include <vector>

#if ME_DIRECTX

#include "Device/IDevice.h"
#include "Device/D3D12Device.h"
#include "ShaderStructures.h"
#include "Utils/StepTimer.h"
#include "Utils/DirectXHelper.h"

namespace Moonlight
{
	class TestModelRenderer
	{
	public:
		TestModelRenderer(D3D12Device* deviceResources);
		void CreateDeviceDependentResources();
		void CreateWindowSizeDependentResources();
		void ReleaseDeviceDependentResources();
		void Update(DX::StepTimer const& timer);
		void Render();
		void StartTracking();
		void TrackingUpdate(float positionX);
		void StopTracking();
		bool IsTracking() { return m_tracking; }


	private:
		void Rotate(float radians);

	private:
		// Cached pointer to device resources.
		D3D12Device* m_deviceResources;
		Microsoft::WRL::ComPtr<ID3D11Buffer>		m_constantBuffer;
		ModelViewProjectionConstantBuffer	m_constantBufferData;

		// Variables used with the rendering loop.
		bool	m_loadingComplete;
		float	m_degreesPerSecond;
		bool	m_tracking;
	};
}

#endif