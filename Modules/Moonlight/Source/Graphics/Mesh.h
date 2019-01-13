#pragma once
#include <string>
#include <vector>

#include "Texture.h"
#include "Shader.h"
#include "Content/ShaderStructures.h"

namespace Moonlight
{
	struct Vertex
	{
		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT3 Normal;
	};

	class Mesh
	{
	public:
		Mesh() = delete;
		~Mesh();

		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;
		std::vector<Texture*> textures;
		unsigned int VAO;

		Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture*> textures);

		// render the mesh
		void Draw(Moonlight::Shader& shader);

	private:
		unsigned int VBO, EBO;

		// System resources for cube geometry.
		uint32	m_indexCount;
		void InitMesh();

		Microsoft::WRL::ComPtr<ID3D11Buffer>		m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>		m_indexBuffer;

	};
}