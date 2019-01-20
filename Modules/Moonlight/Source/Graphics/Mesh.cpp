#include "Mesh.h"
#include <iostream>

#include "Content/ShaderStructures.h"
#include "Device/D3D12Device.h"
#include "Logger.h"
#include "Renderer.h"
#include "Utils/DirectXHelper.h"
#include <DirectXMath.h>
#include "Shader.h"
#include "Game.h"

namespace Moonlight
{
	Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture*> textures)
		: m_indexCount(0)
	{
		this->vertices = vertices;
		this->indices = indices;
		this->textures = textures;

		InitMesh();
	}

	Mesh::~Mesh()
	{
		m_vertexBuffer.Reset();
		m_indexBuffer.Reset();
	}

	void Mesh::Draw(Moonlight::Shader& shader)
	{
		if (!shader.IsLoaded())
		{
			return;
		}
		auto context = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice()).GetD3DDeviceContext();
		// Each vertex is one instance of the VertexPositionColor struct.
		UINT stride = sizeof(Vertex);
		UINT offset = 0;
		context->IASetVertexBuffers(
			0,
			1,
			m_vertexBuffer.GetAddressOf(),
			&stride,
			&offset
		);

		context->IASetIndexBuffer(
			m_indexBuffer.Get(),
			DXGI_FORMAT_R32_UINT, // Each index is one 16-bit unsigned integer (short).
			0
		);

		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		shader.Use();

		// Draw the objects.
		context->DrawIndexed(
			m_indexCount,
			0,
			0
		);
	}

	void Mesh::InitMesh()
	{
		// Load mesh vertices. Each vertex has a position and a color.
		static const VertexPositionColor cubeVertices[] =
		{
			{DirectX::XMFLOAT3(-0.5f, -0.5f, -0.5f), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f)},
			{DirectX::XMFLOAT3(-0.5f, -0.5f,  0.5f), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f)},
			{DirectX::XMFLOAT3(-0.5f,  0.5f, -0.5f), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f)},
			{DirectX::XMFLOAT3(-0.5f,  0.5f,  0.5f), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f)},
			{DirectX::XMFLOAT3(0.5f, -0.5f, -0.5f), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f)},
			{DirectX::XMFLOAT3(0.5f, -0.5f,  0.5f), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f)},
			{DirectX::XMFLOAT3(0.5f,  0.5f, -0.5f), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f)},
			{DirectX::XMFLOAT3(0.5f,  0.5f,  0.5f), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f)},
		};
		auto device = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice());
		D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
		vertexBufferData.pSysMem = &vertices[0];
		vertexBufferData.SysMemPitch = 0;
		vertexBufferData.SysMemSlicePitch = 0;
		CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(Vertex) * vertices.size(), D3D11_BIND_VERTEX_BUFFER);
		DX::ThrowIfFailed(
			device.GetD3DDevice()->CreateBuffer(
				&vertexBufferDesc,
				&vertexBufferData,
				&m_vertexBuffer
			)
		);

		// Load mesh indices. Each trio of indices represents
		// a triangle to be rendered on the screen.
		// For example: 0,2,1 means that the vertices with indexes
		// 0, 2 and 1 from the vertex buffer compose the 
		// first triangle of this mesh.
		static const unsigned short cubeIndices[] =
		{
			0,2,1, // -x
			1,2,3,

			4,5,6, // +x
			5,7,6,

			0,1,5, // -y
			0,5,4,

			2,6,7, // +y
			2,7,3,

			0,4,6, // -z
			0,6,2,

			1,3,7, // +z
			1,7,5,
		};

		m_indexCount = indices.size(); // ARRAYSIZE(cubeIndices);

		D3D11_SUBRESOURCE_DATA indexBufferData = { 0 };
		indexBufferData.pSysMem = &indices[0];
		indexBufferData.SysMemPitch = 0;
		indexBufferData.SysMemSlicePitch = 0;
		CD3D11_BUFFER_DESC indexBufferDesc(sizeof(unsigned int) * m_indexCount, D3D11_BIND_INDEX_BUFFER);
		DX::ThrowIfFailed(
			device.GetD3DDevice()->CreateBuffer(
				&indexBufferDesc,
				&indexBufferData,
				&m_indexBuffer
			)
		);
	}
}