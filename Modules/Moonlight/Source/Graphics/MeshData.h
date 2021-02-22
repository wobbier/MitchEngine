#pragma once
#include <string>
#include <vector>

#include "Texture.h"
#include "Graphics/ShaderStructures.h"
#include <d3d11.h>
#include "Material.h"
#include "Pointers.h"

namespace Moonlight
{
	class Material;

	class MeshData
	{
	public:
		MeshData() = default;
		~MeshData();

		std::vector<Vertex> vertices;
		std::vector<uint16_t> indices;
		SharedPtr<Material> material;
		unsigned int VAO;

		MeshData(std::vector<Vertex> vertices, std::vector<uint16_t> indices, SharedPtr<Material> newMaterial = nullptr);

		// render the mesh
		//void Draw(SharedPtr<Material> mat, ID3D11DeviceContext* context, bool depthOnly = false);

		std::vector<MeshData*> Children;
		std::string Name;

	protected:
		void InitMesh();

	private:
		// System resources for cube geometry.
		unsigned int m_indexCount;

		//Microsoft::WRL::ComPtr<ID3D11Buffer>		m_vertexBuffer;
		//Microsoft::WRL::ComPtr<ID3D11Buffer>		m_indexBuffer;

	};
}