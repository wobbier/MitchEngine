#include "SkyBox.h"
#include "Resource/ResourceCache.h"
#include "Texture.h"
#include <DirectXMath.h>
#include "MeshData.h"
#include "ShaderCommand.h"
#include <WICTextureLoader.h>
#include "Game.h"
#include "Material.h"
#include "Engine/Engine.h"

using namespace DirectX;

namespace Moonlight
{
	SkyBox::SkyBox(const std::string& InPath)
	{
		Path SystemPath(InPath);

		//SkyMap = ResourceCache::GetInstance().Get<Texture>(SystemPath);
		//SkyModel = ResourceCache::GetInstance().Get<ModelResource>(Path("Assets/Skybox.fbx"));
		//SkyMaterial = std::make_shared<Material>("Unlit", "Assets/Shaders/UnlitShader.hlsl");
		//SkyMaterial->SetTexture(TextureType::Diffuse, SkyMap);

		//auto device = static_cast<DX11Device&>(GetEngine().GetRenderer().GetDevice());

		//D3D11_RASTERIZER_DESC cmdesc;

		//ZeroMemory(&cmdesc, sizeof(D3D11_RASTERIZER_DESC));
		//cmdesc.FillMode = D3D11_FILL_SOLID;
		//cmdesc.CullMode = D3D11_CULL_BACK;
		//cmdesc.DepthClipEnable = TRUE;
		//cmdesc.FrontCounterClockwise = true;
		//DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &CCWcullMode));

		//cmdesc.FrontCounterClockwise = false;

		//DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &CWcullMode));

		//cmdesc.CullMode = D3D11_CULL_NONE;
		//DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &RSCullNone));

		//D3D11_DEPTH_STENCIL_DESC dssDesc;
		//ZeroMemory(&dssDesc, sizeof(D3D11_DEPTH_STENCIL_DESC));
		//dssDesc.DepthEnable = false;
		//dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		//dssDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

		//device.GetD3DDevice()->CreateDepthStencilState(&dssDesc, &NoDepth);
	}

	void SkyBox::Draw(ID3D11DeviceContext* context)
	{
		//SkyMaterial->MeshShader.Use(context);

		//context->OMSetDepthStencilState(NoDepth, 0);
		//context->RSSetState(RSCullNone);

		//SkyModel->RootNode.Nodes[0].Meshes[0]->Draw(SkyMaterial, context);

		//context->OMSetDepthStencilState(NULL, 0);
	}
}
