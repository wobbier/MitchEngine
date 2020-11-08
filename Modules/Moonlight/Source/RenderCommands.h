#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "Math/Matrix4.h"
#include <string>
#include "Pointers.h"

namespace Moonlight { class ShaderCommand; }

namespace Moonlight { class MeshData; }
namespace Moonlight { class Material; }
namespace Moonlight { class SkyBox; }

namespace Moonlight
{
	enum MeshType
	{
		Model = 0,
		Plane,
		Cube,
		MeshCount
	};

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
		MeshType Type = MeshType::Model;
		SharedPtr<Material> MeshMaterial;
		DirectX::SimpleMath::Matrix Transform;
		bool Visible = false;
	};

	struct LightCommand
	{
		DirectX::XMFLOAT4 pos;
		DirectX::XMFLOAT4 dir;
		DirectX::XMFLOAT4 cameraPos;
		DirectX::XMFLOAT4 ambient;
		DirectX::XMFLOAT4 diffuse;
	};
}