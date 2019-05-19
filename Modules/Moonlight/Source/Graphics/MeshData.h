#pragma once
#include <string>
#include <vector>

#include "Texture.h"
#include "Content/ShaderStructures.h"
#include <DirectXMath.h>
#include <wrl/event.h>
#include <d3d11.h>
#include "Material.h"

namespace Moonlight
{
	struct Vertex
	{
		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT3 Normal;
		DirectX::XMFLOAT2 TextureCoord;
	};

	class MeshData
	{
	public:
		MeshData() = delete;
		~MeshData();

		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;
		Material* material;
		unsigned int VAO;

		MeshData(std::vector<Vertex> vertices, std::vector<unsigned int> indices, Material* newMaterial);

		// render the mesh
		void Draw(class Material* mat);

		std::vector<MeshData*> Children;
		std::string Name;
	private:
		// System resources for cube geometry.
		unsigned int m_indexCount;
		void InitMesh();

		Microsoft::WRL::ComPtr<ID3D11Buffer>		m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>		m_indexBuffer;

	};
}