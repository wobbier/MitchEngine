#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "Math/Vector2.h"

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

	struct MeshCommand
	{
		MeshData* SingleMesh = nullptr;
		ShaderCommand* MeshShader = nullptr;
		Material* MeshMaterial = nullptr;
		DirectX::XMMATRIX Transform;
	};

	struct CameraData
	{
		Vector3 Position;
		Vector3 Front;
		Vector3 Up;
		Vector2 OutputSize;
		float FOV;
		SkyBox* Skybox = nullptr;
	};

	struct LightCommand
	{
		DirectX::XMFLOAT4 dir;
		DirectX::XMFLOAT4 ambient;
		DirectX::XMFLOAT4 diffuse;
	};
}