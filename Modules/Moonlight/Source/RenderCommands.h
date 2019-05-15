#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "Math/Vector2.h"

namespace Moonlight { class Shader; }

namespace Moonlight { class MeshData; }

namespace Moonlight
{
	struct ModelCommand
	{
		std::vector<MeshData*> Meshes;
		Shader* ModelShader;
		DirectX::XMMATRIX Transform;
	};

	struct MeshCommand
	{
		MeshData* SingleMesh = nullptr;
		Shader* MeshShader = nullptr;
		DirectX::XMMATRIX Transform;
	};

	struct CameraData
	{
		Vector3 Position;
		Vector3 Front;
		Vector3 Up;
		Vector2 OutputSize;
		float FOV;
	};

	struct LightCommand
	{
		DirectX::XMFLOAT4 dir;
		DirectX::XMFLOAT4 ambient;
		DirectX::XMFLOAT4 diffuse;
	};
}