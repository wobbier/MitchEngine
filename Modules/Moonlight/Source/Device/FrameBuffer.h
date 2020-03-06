#pragma once
#include <d3d11.h>
#include <wrl/client.h>

namespace Moonlight
{
	struct FrameBuffer
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> FinalTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> ColorTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> NormalTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> SpecularTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> DepthStencilTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> PositionTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> ShadowMapTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> UITexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> RenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> ColorRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> NormalRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> SpecularRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> PositionRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> ShadowMapRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> UIRenderTargetView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ColorShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> NormalShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SpecularShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> DepthShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PositionShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShadowMapShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> UIShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> DepthStencilView;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> PickingTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> PickingTargetView;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PickingResourceView;

		UINT Width = 0;
		UINT Height = 0;
		UINT Samples = 0;
	};
}