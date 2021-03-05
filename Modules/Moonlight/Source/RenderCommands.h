#pragma once
#include <vector>
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
		Matrix4 Transform;
	};

	struct DebugColliderCommand
	{
		float test;

		//DirectX::SimpleMath::Matrix Transform;
	};

	struct MeshCommand
	{
		MeshCommand() = default;
		void Reset()
		{
			SingleMesh = nullptr;
			MeshMaterial = nullptr;
			Transform = glm::mat4(1.f);
		}
		MeshData* SingleMesh = nullptr;
		MeshType Type = MeshType::Model;
		SharedPtr<Material> MeshMaterial =nullptr;
		glm::mat4 Transform;
		bool Visible = false;
	};

	struct LightCommand
	{
		float test;
		/*DirectX::XMFLOAT4 pos;
		DirectX::XMFLOAT4 dir;
		DirectX::XMFLOAT4 cameraPos;
		DirectX::XMFLOAT4 ambient;
		DirectX::XMFLOAT4 diffuse;*/
	};
}