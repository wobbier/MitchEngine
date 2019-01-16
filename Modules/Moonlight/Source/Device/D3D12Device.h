#pragma once
#include "IDevice.h"

#ifdef ME_DIRECTX

#include <d3d11_3.h>
#include <d2d1_3.h>
#include <dxgi1_4.h>
#include <dwrite_3.h>
#include <DirectXMath.h>
#include <wincodec.h>
#include <wrl/client.h>

#if ME_PLATFORM_UWP
#include <agile.h>
#endif

namespace Moonlight
{
	struct TSize
	{
		int Width;
		int Height;
	};
	class D3D12Device
		: public IDevice
	{
	public:
		D3D12Device();
#if ME_PLATFORM_UWP
		void SetWindow(Windows::UI::Core::CoreWindow^ window);
		void SetCurrentOrientation(Windows::Graphics::Display::DisplayOrientations currentOrientation);
#endif
#if ME_PLATFORM_WIN64
		void SetWindow(HWND window);
#endif

		// The size of the render target, in pixels.
		TSize GetOutputSize() const { return m_outputSize; }
		virtual void SetLogicalSize(glm::vec2 logicalSize);
		void SetDpi(float dpi);
		void ValidateDevice();
		void HandleDeviceLost();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);
		void Trim();

		virtual void PreRender() final;
		virtual void Present() final;

		// The size of the render target, in dips.
		glm::vec2					GetLogicalSize() const { return m_logicalSize; }
		float						GetDpi() const { return m_effectiveDpi; }

		// D3D Accessors.
		ID3D11Device3*				GetD3DDevice() const { return m_d3dDevice.Get(); }
		ID3D11DeviceContext3*		GetD3DDeviceContext() const { return m_d3dContext.Get(); }
		IDXGISwapChain3*			GetSwapChain() const { return m_swapChain.Get(); }
		D3D_FEATURE_LEVEL			GetDeviceFeatureLevel() const { return m_d3dFeatureLevel; }
		ID3D11RenderTargetView1*	GetBackBufferRenderTargetView() const { return m_d3dRenderTargetView.Get(); }
		ID3D11DepthStencilView*		GetDepthStencilView() const { return m_d3dDepthStencilView.Get(); }
		D3D11_VIEWPORT				GetScreenViewport() const { return m_screenViewport; }
		DirectX::XMFLOAT4X4			GetOrientationTransform3D() const { return m_orientationTransform3D; }
		// D2D Accessors.
		ID2D1Factory3*				GetD2DFactory() const { return m_d2dFactory.Get(); }
		ID2D1Device2*				GetD2DDevice() const { return m_d2dDevice.Get(); }
		ID2D1DeviceContext2*		GetD2DDeviceContext() const { return m_d2dContext.Get(); }
		ID2D1Bitmap1*				GetD2DTargetBitmap() const { return m_d2dTargetBitmap.Get(); }
		IDWriteFactory3*			GetDWriteFactory() const { return m_dwriteFactory.Get(); }
		IWICImagingFactory2*		GetWicImagingFactory() const { return m_wicFactory.Get(); }
		D2D1::Matrix3x2F			GetOrientationTransform2D() const { return m_orientationTransform2D; }

	private:
		virtual void CreateDeviceIndependentResources() final;
		virtual void CreateDeviceResources() final;
		virtual void CreateWindowSizeDependentResources() final;
		void UpdateRenderTargetSize();
		DXGI_MODE_ROTATION ComputeDisplayRotation();

		// Direct3D objects.
		Microsoft::WRL::ComPtr<ID3D11Device3>			m_d3dDevice;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext3>	m_d3dContext;
		Microsoft::WRL::ComPtr<IDXGISwapChain3>			m_swapChain;
		ID3D11RasterizerState*							WireFrame;

		// Direct3D rendering objects. Required for 3D.
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView1>	m_d3dRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView>	m_d3dDepthStencilView;
		D3D11_VIEWPORT									m_screenViewport;

		// Direct2D drawing components.
		Microsoft::WRL::ComPtr<ID2D1Factory3>		m_d2dFactory;
		Microsoft::WRL::ComPtr<ID2D1Device2>		m_d2dDevice;
		Microsoft::WRL::ComPtr<ID2D1DeviceContext2>	m_d2dContext;
		Microsoft::WRL::ComPtr<ID2D1Bitmap1>		m_d2dTargetBitmap;

		// DirectWrite drawing components.
		Microsoft::WRL::ComPtr<IDWriteFactory3>		m_dwriteFactory;
		Microsoft::WRL::ComPtr<IWICImagingFactory2>	m_wicFactory;

		// Cached reference to the Window.
#if ME_PLATFORM_UWP
		Platform::Agile<Windows::UI::Core::CoreWindow> m_window;
		Windows::Graphics::Display::DisplayOrientations	m_nativeOrientation;
		Windows::Graphics::Display::DisplayOrientations	m_currentOrientation;
#endif

#if ME_PLATFORM_WIN64
		HWND m_window;
#endif

		// Cached device properties.
		D3D_FEATURE_LEVEL								m_d3dFeatureLevel;
		glm::vec2										m_logicalSize;
		float											m_dpi;
		TSize						m_d3dRenderTargetSize;
		TSize						m_outputSize;

		// This is the DPI that will be reported back to the app. It takes into account whether the app supports high resolution screens or not.
		float m_effectiveDpi;

		// Transforms used for display orientation.
		D2D1::Matrix3x2F	m_orientationTransform2D;
		DirectX::XMFLOAT4X4	m_orientationTransform3D;

		// The IDeviceNotify can be held directly as it owns the DeviceResources.
		IDeviceNotify* m_deviceNotify;
	};
}

#endif