#pragma once
#include "FilePath.h"
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/module.h>
#include <wrl/client.h>
#include "MeshData.h"
#include "Graphics/ModelResource.h"

using namespace DirectX;

namespace Moonlight
{
	class SkyBox
	{
	public:
		SkyBox(const std::string& Path);

		std::shared_ptr<class Texture> SkyMap = nullptr;
		class Shader* SkyShader = nullptr;

		void Draw();

	private:
		ID3D11RasterizerState* CCWcullMode;
		ID3D11RasterizerState* CWcullMode;

		ID3D11DepthStencilState* NoDepth;
		ID3D11RasterizerState* RSCullNone;

		std::shared_ptr<class ModelResource> SkyModel;
	};
}