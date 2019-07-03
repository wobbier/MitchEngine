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
#include <memory>
#include "Math/Vector2.h"
#include "Path.h"

namespace Moonlight
{
	struct FrameBuffer
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> FinalTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> ColorTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> NormalTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> SpecularTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> DepthStencilTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> RenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> ColorRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> NormalRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> SpecularRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ColorShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> NormalShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SpecularShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> DepthStencilView;
		UINT Width = 0;
		UINT Height = 0;
		UINT Samples = 0;
	};

	struct ShaderProgram
	{
		Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;
	};

	struct ComputeProgram
	{
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> ComputeShader;
	};

	class D3D12Device
		: public IDevice
	{
	public:
		D3D12Device();

		// The size of the render target, in pixels.
		Vector2 GetOutputSize() const { return m_outputSize; }
		virtual void SetLogicalSize(Vector2 logicalSize);
		void ValidateDevice();
		void HandleDeviceLost();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);
		void Trim();

		virtual void WindowSizeChanged(const Vector2& NewSize) final;

		const int GetMSAASamples() const;
		void ResetCullingMode() const;
		// The size of the render target, in dips.
		Vector2					GetLogicalSize() const { return m_logicalSize; }

		// D3D Accessors.
		ID3D11Device3*				GetD3DDevice() const { return m_d3dDevice.Get(); }
		ID3D11DeviceContext3*		GetD3DDeviceContext() const { return m_d3dContext.Get(); }
		IDXGISwapChain3*			GetSwapChain() const { return m_swapChain.Get(); }
		D3D_FEATURE_LEVEL			GetDeviceFeatureLevel() const { return m_d3dFeatureLevel; }
		ID3D11RenderTargetView*	GetBackBufferRenderTargetView() const { return m_d3dRenderTargetView.Get(); }
		//ID3D11DepthStencilView*		GetDepthStencilView() const { return m_d3dDepthStencilView.Get(); }
		D3D11_VIEWPORT				GetScreenViewport() const { return m_screenViewport; }
		Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const Path& FileName, const std::string& EntryPoint, const std::string& Profile);

		ShaderProgram CreateShaderProgram(const Microsoft::WRL::ComPtr<ID3DBlob>& vsBytecode, const Microsoft::WRL::ComPtr<ID3DBlob>& psBytecode, const std::vector<D3D11_INPUT_ELEMENT_DESC>* inputLayoutDesc) const;

		ComputeProgram CreateComputeProgram(const Microsoft::WRL::ComPtr<ID3DBlob>& csBytecode) const;

		Microsoft::WRL::ComPtr<ID3D11SamplerState> CreateSamplerState(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressMode) const;

		// D2D Accessors.
		IDWriteFactory3*			GetDWriteFactory() const { return m_dwriteFactory.Get(); }
		IWICImagingFactory2*		GetWicImagingFactory() const { return m_wicFactory.Get(); }
		void SetOutputSize(Vector2 NewSize);

		void InitD2DScreenTexture();
		FrameBuffer* CreateFrameBuffer(UINT Width, UINT Height, UINT Samples, DXGI_FORMAT ColorFormat, DXGI_FORMAT DepthStencilFormat) const;

		virtual void CreateWindowSizeDependentResources() final;
#if ME_PLATFORM_WIN64
		HWND m_window;
#endif
		ID3D11Buffer *d2dIndexBuffer;
		ID3D11Buffer *d2dVertBuffer;

		Microsoft::WRL::ComPtr<ID3D11RenderTargetView>	m_d3dRenderTargetView;
		ID3D11BlendState* TransparentBlendState = nullptr;
	private:
		virtual void CreateFactories() final;
		virtual void CreateDeviceResources() final;
		void UpdateRenderTargetSize();


		// Direct3D objects.
		Microsoft::WRL::ComPtr<ID3D11Device3>			m_d3dDevice;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext3>	m_d3dContext;
		Microsoft::WRL::ComPtr<IDXGISwapChain3>			m_swapChain;
		ID3D11RasterizerState*							WireFrame;
		ID3D11RasterizerState*							FrontFaceCulled;

		// Direct3D rendering objects. Required for 3D.
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView>	m_d3dDepthStencilView;
		D3D11_VIEWPORT									m_screenViewport;

		// DirectWrite drawing components.
		Microsoft::WRL::ComPtr<IDWriteFactory3>		m_dwriteFactory;
		Microsoft::WRL::ComPtr<IWICImagingFactory2>	m_wicFactory;


		// Cached reference to the Window.
#if ME_PLATFORM_UWP
		Platform::Agile<Windows::UI::Core::CoreWindow> m_window;
#endif

		// Cached device properties.
		D3D_FEATURE_LEVEL								m_d3dFeatureLevel;
		Vector2										m_logicalSize;
		Vector2						m_d3dRenderTargetSize;
		Vector2						m_outputSize;

		// The IDeviceNotify can be held directly as it owns the DeviceResources.
		IDeviceNotify* m_deviceNotify;
		const int MaxMSAASamples = 16;
		int MSAASamples = 1;
	};
}

#endif