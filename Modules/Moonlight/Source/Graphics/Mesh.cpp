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
	Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, Material* newMaterial)
		: m_indexCount(0)
		, material(newMaterial)
	{
		this->vertices = vertices;
		this->indices = indices;

		InitMesh();
	}

	Mesh::~Mesh()
	{
		m_vertexBuffer.Reset();
		m_indexBuffer.Reset();
	}

	void Mesh::InitMesh()
	{
		auto& device = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice());

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

		m_indexCount = indices.size();

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

	void Mesh::Draw()
	{
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

		if (material->GetTexture("DiffuseColor"))
		{
			context->PSSetShaderResources(0, 1, &material->GetTexture("DiffuseColor")->CubesTexture);
			context->PSSetSamplers(0, 1, &material->GetTexture("DiffuseColor")->CubesTexSamplerState);
		}

		// Draw the objects.
		context->DrawIndexed(
			m_indexCount,
			0,
			0
		);
	}
}