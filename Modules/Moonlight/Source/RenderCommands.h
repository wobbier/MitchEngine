#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "Math/Matirx4.h"
#include <string>

namespace Moonlight { class ShaderCommand; }

namespace Moonlight { class MeshData; }
namespace Moonlight { class Material; }
namespace Moonlight { class SkyBox; }

namespace Moonlight
{
	struct ModelCommand
	{
		std::vector<MeshData*> Meshes;
		ShaderCommand* ModelShader;
		DirectX::XMMATRIX Transform;
	};

	struct DebugColliderCommand
	{
		DirectX::SimpleMath::Matrix Transform;
	};

	struct MeshCommand
	{
		MeshData* SingleMesh = nullptr;
		ShaderCommand* MeshShader = nullptr;
		Material* MeshMaterial = nullptr;
		DirectX::SimpleMath::Matrix Transform;
	};

	struct TextCommand
	{
		std::wstring SourceText;
		Vector2 ScreenPosition;
		Vector2 Anchor;
	};

	struct LightCommand
	{
		DirectX::XMFLOAT4 pos;
		DirectX::XMFLOAT4 dir;
		DirectX::XMFLOAT4 cameraPos;
		DirectX::XMFLOAT4 ambient;
		DirectX::XMFLOAT4 diffuse;
	};

	struct LightingPassConstantBuffer
	{
		LightCommand Light;
		DirectX::XMMATRIX LightSpaceMatrix;
	};
}