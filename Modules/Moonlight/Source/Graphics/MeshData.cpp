#include "MeshData.h"
#include <iostream>

#include "Content/ShaderStructures.h"
#include "Device/D3D12Device.h"
#include "Logger.h"
#include "Renderer.h"
#include "Utils/DirectXHelper.h"
#include <DirectXMath.h>
#include "ShaderCommand.h"
#include "Game.h"
#include "optick.h"
#include "Material.h"
#include "Engine/Engine.h"

namespace Moonlight
{
	MeshData::MeshData(std::vector<Vertex> vertices, std::vector<unsigned int> indices, Material* mat = nullptr)
		: m_indexCount(0)
		, material(mat)
	{
		this->vertices = vertices;
		this->indices = indices;

		InitMesh();
	}

	MeshData::~MeshData()
	{
		m_vertexBuffer.Reset();
		m_indexBuffer.Reset();
	}

	void MeshData::InitMesh()
	{
		auto& device = static_cast<D3D12Device&>(GetEngine().GetRenderer().GetDevice());

		D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
		vertexBufferData.pSysMem = &vertices[0];
		vertexBufferData.SysMemPitch = 0;
		vertexBufferData.SysMemSlicePitch = 0;
		CD3D11_BUFFER_DESC vertexBufferDesc(static_cast<UINT>(sizeof(Vertex) * vertices.size()), D3D11_BIND_VERTEX_BUFFER);
		DX::ThrowIfFailed(
			device.GetD3DDevice()->CreateBuffer(
				&vertexBufferDesc,
				&vertexBufferData,
				&m_vertexBuffer
			)
		);

		m_indexCount = static_cast<unsigned int>(indices.size());

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

	void MeshData::Draw(Material* mat)
	{
		OPTICK_EVENT("Mesh::Draw", Optick::Category::Rendering);

		auto context = static_cast<D3D12Device&>(GetEngine().GetRenderer().GetDevice()).GetD3DDeviceContext();

		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		{
			OPTICK_EVENT("Mesh::Draw::Setup");
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
		}
		if (!mat)
		{
			mat = material;
		}
		if (mat)
		{
			OPTICK_EVENT("Mesh::Draw::Texture", Optick::Category::Rendering);
			const Texture* diffuse = mat->GetTexture(TextureType::Diffuse);
			if (diffuse)
			{
				OPTICK_EVENT("Mesh::Draw::Texture::ShaderResources");
				context->PSSetShaderResources(0, 1, &diffuse->ShaderResourceView);
				ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
				if (mat->GetTexture(TextureType::Normal))
				{
					context->PSSetShaderResources(1, 1, &mat->GetTexture(TextureType::Normal)->ShaderResourceView);
				}
				else
				{
					context->PSSetShaderResources(1, 1, nullSRV);
				}
				if (mat->GetTexture(TextureType::Opacity))
				{
					context->PSSetShaderResources(2, 1, &mat->GetTexture(TextureType::Opacity)->ShaderResourceView);
				}
				else
				{
					context->PSSetShaderResources(2, 1, nullSRV);
				}
				if (mat->GetTexture(TextureType::Specular))
				{
					context->PSSetShaderResources(3, 1, &mat->GetTexture(TextureType::Specular)->ShaderResourceView);
				}
				else
				{
					context->PSSetShaderResources(3, 1, nullSRV);
				}
				context->PSSetSamplers(0, 1, diffuse->SamplerState.GetAddressOf());
			}
		}                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       

		{
			OPTICK_EVENT("Mesh::Draw::DrawCall");
			// Draw the objects.
			context->DrawIndexed(
				m_indexCount,
				0,
				0
			);
		}
	}
}