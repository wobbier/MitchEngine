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

	class Mesh
	{
	public:
		Mesh() = delete;
		~Mesh();

		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;
		Material* material;
		unsigned int VAO;

		Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, Material* newMaterial);

		// render the mesh
		void Draw();

	private:
		// System resources for cube geometry.
		unsigned int m_indexCount;
		void InitMesh();

		Microsoft::WRL::ComPtr<ID3D11Buffer>		m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>		m_indexBuffer;

	};
}