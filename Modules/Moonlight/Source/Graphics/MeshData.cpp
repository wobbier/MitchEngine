#include "MeshData.h"

#include "Graphics/ShaderStructures.h"
#include "CLog.h"
#include "ShaderCommand.h"
#include "Material.h"

namespace Moonlight
{
    MeshData::MeshData( std::vector<PosNormTexTanBiVertex> inVerticies, std::vector<uint32_t> inIndices, SharedPtr<Moonlight::Material> inMaterial )
        : Vertices( inVerticies )
        , Indices( inIndices )
        , MeshMaterial( inMaterial )
    {
        InitMesh();
    }

    MeshData::~MeshData()
    {
        if( bgfx::isValid( m_vbh ) )
            bgfx::destroy( m_vbh );

        if( bgfx::isValid( m_ibh ) )
            bgfx::destroy( m_ibh );
    }

    void MeshData::InitMesh()
    {
        m_vertexCount = static_cast<uint32_t>( Vertices.size() );
        m_indexCount = static_cast<uint32_t>( Indices.size() );

        m_vbh = bgfx::createVertexBuffer( bgfx::copy( Vertices.data(), static_cast<uint32_t>( sizeof( Moonlight::PosNormTexTanBiVertex ) * Vertices.size() ) ), Moonlight::PosNormTexTanBiVertex::ms_layout );
        if( !bgfx::isValid( m_vbh ) )
        {
            ME_ASSERT_MSG( false, "Ran out of vbh?" );
        }

        // 32-bit indices: unwelded imports can exceed 65535 vertices per mesh.
        m_ibh = bgfx::createIndexBuffer( bgfx::copy( Indices.data(), static_cast<uint32_t>( sizeof( uint32_t ) * Indices.size() ) ), BGFX_BUFFER_INDEX32 );

        // bgfx owns copies of the geometry now; drop the CPU side.
        std::vector<PosNormTexTanBiVertex>().swap( Vertices );
        std::vector<uint32_t>().swap( Indices );
    }

    void MeshData::Draw( SharedPtr<Material> inMaterial )
    {

    }

//	void MeshData::Draw(SharedPtr<Material> mat, ID3D11DeviceContext* context, bool depthOnly)
//	{
//		OPTICK_EVENT("Mesh::Draw", Optick::Category::Rendering);
//
//// 		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
//// 
//// 		{
//// 			OPTICK_EVENT("Mesh::Draw::Setup");
//// 			// Each vertex is one instance of the VertexPositionColor struct.
//// 			UINT stride = sizeof(Vertex);
//// 			UINT offset = 0;
//// 			context->IASetVertexBuffers(
//// 				0,
//// 				1,
//// 				m_vertexBuffer.GetAddressOf(),
//// 				&stride,
//// 				&offset
//// 			);
//// 
//// 			context->IASetIndexBuffer(
//// 				m_indexBuffer.Get(),
//// 				DXGI_FORMAT_R16_UINT, // Each index is one 16-bit unsigned integer (short).
//// 				0
//// 			);
//// 		}
//// 		if (!mat)
//// 		{
//// 			mat = material;
//// 		}
//// 		if (mat && !depthOnly)
//// 		{
//// 			OPTICK_EVENT("Mesh::Draw::Texture", Optick::Category::Rendering);
//// 			const Texture* diffuse = mat->GetTexture(TextureType::Diffuse);
//// 			if (diffuse)
//// 			{
//// 				OPTICK_EVENT("Mesh::Draw::Texture::ShaderResources");
//// 				context->PSSetShaderResources(0, 1, &diffuse->ShaderResourceView);
//// 				ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
//// 				if (mat->GetTexture(TextureType::Normal))
//// 				{
//// 					context->PSSetShaderResources(1, 1, &mat->GetTexture(TextureType::Normal)->ShaderResourceView);
//// 				}
//// 				else
//// 				{
//// 					context->PSSetShaderResources(1, 1, nullSRV);
//// 				}
//// 				if (mat->GetTexture(TextureType::Opacity))
//// 				{
//// 					context->PSSetShaderResources(2, 1, &mat->GetTexture(TextureType::Opacity)->ShaderResourceView);
//// 				}
//// 				else
//// 				{
//// 					context->PSSetShaderResources(2, 1, nullSRV);
//// 				}
//// 				if (mat->GetTexture(TextureType::Specular))
//// 				{
//// 					context->PSSetShaderResources(3, 1, &mat->GetTexture(TextureType::Specular)->ShaderResourceView);
//// 				}
//// 				else
//// 				{
//// 					context->PSSetShaderResources(3, 1, nullSRV);
//// 				}
//// 				context->PSSetSamplers(0, 1, diffuse->SamplerState.GetAddressOf());
//// 			}
//// 		}                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       
//// 
//// 		{
//// 			OPTICK_EVENT("Mesh::Draw::DrawCall");
//// 			// Draw the objects.
//// 			context->DrawIndexed(
//// 				m_indexCount,
//// 				0,
//// 				0
//// 			);
//// 		}
//	}
}