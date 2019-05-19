#include "SkyBox.h"
#include "Resource/ResourceCache.h"
#include "Texture.h"
#include <DirectXMath.h>
#include "MeshData.h"
#include "ShaderCommand.h"
#include <WICTextureLoader.h>
#include "Game.h"
#include "Material.h"

using namespace DirectX;

namespace Moonlight
{
	SkyBox::SkyBox(const std::string& Path)
	{
		FilePath SystemPath(Path);

		SkyMap = ResourceCache::GetInstance().Get<Texture>(FilePath(Path));
		SkyShader = new ShaderCommand("Assets/Shaders/SimpleVertexShader.cso", "Assets/Shaders/SimplePixelShader.cso");
		SkyModel = ResourceCache::GetInstance().Get<ModelResource>(FilePath("Assets/Skybox.fbx"));
		SkyMaterial = new Material();
		SkyMaterial->SetTexture(TextureType::Diffuse, SkyMap);

		auto device = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice());

		D3D11_RASTERIZER_DESC cmdesc;

		ZeroMemory(&cmdesc, sizeof(D3D11_RASTERIZER_DESC));
		cmdesc.FillMode = D3D11_FILL_SOLID;
		cmdesc.CullMode = D3D11_CULL_BACK;
		cmdesc.FrontCounterClockwise = true;
		DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &CCWcullMode));

		cmdesc.FrontCounterClockwise = false;

		DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &CWcullMode));

		cmdesc.CullMode = D3D11_CULL_NONE;
		DX::ThrowIfFailed(device.GetD3DDevice()->CreateRasterizerState(&cmdesc, &RSCullNone));

		D3D11_DEPTH_STENCIL_DESC dssDesc;
		ZeroMemory(&dssDesc, sizeof(D3D11_DEPTH_STENCIL_DESC));
		dssDesc.DepthEnable = false;
		dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dssDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

		device.GetD3DDevice()->CreateDepthStencilState(&dssDesc, &NoDepth);
	}

	void SkyBox::Draw()
	{
		auto device = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice()).GetD3DDeviceContext();

		SkyShader->Use();

		device->OMSetDepthStencilState(NoDepth, 0);
		device->RSSetState(RSCullNone);

		SkyModel->RootNode.Nodes[0].Meshes[0]->Draw(SkyMaterial);

		device->OMSetDepthStencilState(NULL, 0);
		return;
	}
}
