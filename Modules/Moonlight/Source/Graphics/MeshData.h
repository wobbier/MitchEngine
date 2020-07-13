#pragma once
#include <string>
#include <vector>

#include "Texture.h"
#include "Graphics/ShaderStructures.h"
#include <DirectXMath.h>
#include <wrl/event.h>
#include <d3d11.h>
#include "Material.h"

namespace Moonlight
{

	class MeshData
	{
	public:
		MeshData() = delete;
		~MeshData();

		std::vector<Vertex> vertices;
		std::vector<uint16_t> indices;
		Material* material;
		unsigned int VAO;

		MeshData(std::vector<Vertex> vertices, std::vector<uint16_t> indices, Material* newMaterial = nullptr);

		// render the mesh
		void Draw(class Material* mat, ID3D11DeviceContext* context, bool depthOnly = false);

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