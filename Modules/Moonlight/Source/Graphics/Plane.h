#pragma once

#include "Path.h"
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/module.h>
#include <wrl/client.h>
#include "MeshData.h"
#include "Graphics/ModelResource.h"
#include "Utils/DirectXHelper.h"
#include "Texture.h"
#include "Material.h"
#include "Scene/Node.h"
#include <memory>

using namespace DirectX;

namespace Moonlight
{
	class Plane
	{
	public:
		Plane(const std::string& InPath, const D3D12Device& device)
		{
			Path SystemPath(InPath);

			SkyMap = ResourceCache::GetInstance().Get<Texture>(SystemPath);
			SkyShader = new ShaderCommand("Assets/Shaders/GridVertexShader.cso", "Assets/Shaders/GridPixelShader.cso");
			SkyModel = ResourceCache::GetInstance().Get<ModelResource>(Path("Assets/Plane.fbx"));
			SkyMaterial = new Material();
			SkyMaterial->SetTexture(TextureType::Diffuse, SkyMap);
			
			D3D11_RASTERIZER_DESC cmdesc;

			ZeroMemory(&cmdesc, sizeof(D3D11_RASTERIZER_DESC));
			cmdesc.FillMode = D3D11_FILL_SOLID;
			cmdesc.CullMode = D3D11_CULL_BACK;
			cmdesc.DepthClipEnable = TRUE;
			cmdesc.FrontCounterClockwise = true;
			DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &CCWcullMode));

			cmdesc.FrontCounterClockwise = false;

			DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &CWcullMode));

			cmdesc.CullMode = D3D11_CULL_NONE;
			DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &RSCullNone));

			D3D11_DEPTH_STENCIL_DESC dsDesc;
			ZeroMemory(&dsDesc, sizeof(D3D11_DEPTH_STENCIL_DESC));
			// Depth test parameters
			dsDesc.DepthEnable = true;
			dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			dsDesc.DepthFunc = D3D11_COMPARISON_LESS;

			// Stencil test parameters
			dsDesc.StencilEnable = true;
			dsDesc.StencilReadMask = 0xFF;
			dsDesc.StencilWriteMask = 0xFF;

			// Stencil operations if pixel is front-facing
			dsDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
			dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

			// Stencil operations if pixel is back-facing
			dsDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			dsDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
			dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			dsDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;


			device.GetD3DDevice()->CreateDepthStencilState(&dsDesc, &NoDepth);

		}

		void Draw(const D3D12Device& device)
		{
			auto context = device.GetD3DDeviceContext();

			SkyShader->Use();

			context->OMSetDepthStencilState(NoDepth, 0);
			context->RSSetState(RSCullNone);

			SkyModel->RootNode.Nodes[0].Meshes[0]->Draw(SkyMaterial);

			//context->OMSetDepthStencilState(NoDepth, 0);
		}


		std::shared_ptr<Texture> SkyMap = nullptr;
		class ShaderCommand* SkyShader = nullptr;
		Material* SkyMaterial = nullptr;

	private:
		ID3D11RasterizerState* CCWcullMode;
		ID3D11RasterizerState* CWcullMode;

		ID3D11DepthStencilState* NoDepth;
		ID3D11RasterizerState* RSCullNone;

		std::shared_ptr<ModelResource> SkyModel;
	};
}