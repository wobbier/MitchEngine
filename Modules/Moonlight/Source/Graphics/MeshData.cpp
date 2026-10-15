#include "MeshData.h"

#include "Graphics/ShaderStructures.h"
#include "CLog.h"
#include "ShaderCommand.h"
#include "Material.h"
#include "Utils/BGFXUtils.h"

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
        if( !IsGpuAlive() )
        {
            return;
        }
        if( bgfx::isValid( m_vbh ) )
            bgfx::destroy( m_vbh );

        if( bgfx::isValid( m_ibh ) )
            bgfx::destroy( m_ibh );

        if( bgfx::isValid( m_skinVbh ) )
            bgfx::destroy( m_skinVbh );

        for( bgfx::IndexBufferHandle lod : m_lodIbh )
        {
            if( bgfx::isValid( lod ) )
            {
                bgfx::destroy( lod );
            }
        }
        for( bgfx::IndexBufferHandle wireframe : m_wireframeIbh )
        {
            if( bgfx::isValid( wireframe ) ) bgfx::destroy( wireframe );
        }
    }


    void MeshData::SetLods( const std::vector<MeshLod>& InLods )
    {
        for( const MeshLod& lod : InLods )
        {
            if( lod.Indices.size() < 3 || m_lodIbh.size() >= 255 )
            {
                continue;
            }
            const bgfx::IndexBufferHandle handle = bgfx::createIndexBuffer( bgfx::copy( lod.Indices.data(), static_cast<uint32_t>( sizeof( uint32_t ) * lod.Indices.size() ) ), BGFX_BUFFER_INDEX32 );
            if( !bgfx::isValid( handle ) )
            {
                break;
            }
            m_lodIbh.push_back( handle );
            m_lodIndices.push_back( lod.Indices );
            m_lodIndexCounts.push_back( static_cast<uint32_t>( lod.Indices.size() ) );
            m_lodHeights.push_back( lod.ScreenHeight );
        }
    }


    bgfx::IndexBufferHandle MeshData::GetWireframeIndexBuffer( uint8_t InLod )
    {
        const size_t level = InLod <= m_lodIndices.size() ? InLod : 0;
        while( m_wireframeIbh.size() <= level ) m_wireframeIbh.push_back( BGFX_INVALID_HANDLE );
        if( bgfx::isValid( m_wireframeIbh[level] ) ) return m_wireframeIbh[level];

        const auto& triangles = level == 0 ? CollisionIndices : m_lodIndices[level - 1];
        if( triangles.empty() ) return BGFX_INVALID_HANDLE;
        std::vector<uint32_t> edges;
        edges.reserve( triangles.size() * 2 );
        for( size_t i = 0; i + 2 < triangles.size(); i += 3 )
        {
            const uint32_t a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            edges.insert( edges.end(), { a, b, b, c, c, a } );
        }
        m_wireframeIbh[level] = bgfx::createIndexBuffer( bgfx::copy( edges.data(), static_cast<uint32_t>( edges.size() * sizeof( uint32_t ) ) ), BGFX_BUFFER_INDEX32 );
        return m_wireframeIbh[level];
    }


    void MeshData::InitSkin( const std::vector<SkinWeightsVertex>& InWeights )
    {
        if( InWeights.empty() || InWeights.size() != m_vertexCount || BoneNames.empty() || BoneNames.size() > kMaxBones )
        {
            return;
        }
        m_skinVbh = bgfx::createVertexBuffer( bgfx::copy( InWeights.data(), static_cast<uint32_t>( sizeof( SkinWeightsVertex ) * InWeights.size() ) ), SkinWeightsVertex::ms_layout );
    }

    void MeshData::InitMesh()
    {
        m_vertexCount = static_cast<uint32_t>( Vertices.size() );

        Bounds = AABB();
        for( const PosNormTexTanBiVertex& vertex : Vertices )
        {
            Bounds.Encapsulate( vertex.Position );
        }
        m_indexCount = static_cast<uint32_t>( Indices.size() );

        m_vbh = bgfx::createVertexBuffer( bgfx::copy( Vertices.data(), static_cast<uint32_t>( sizeof( Moonlight::PosNormTexTanBiVertex ) * Vertices.size() ) ), Moonlight::PosNormTexTanBiVertex::ms_layout );
        if( !bgfx::isValid( m_vbh ) )
        {
            ME_ASSERT_MSG( false, "Ran out of vbh?" );
        }

        // 32-bit indices: unwelded imports can exceed 65535 vertices per mesh.
        m_ibh = bgfx::createIndexBuffer( bgfx::copy( Indices.data(), static_cast<uint32_t>( sizeof( uint32_t ) * Indices.size() ) ), BGFX_BUFFER_INDEX32 );

        // bgfx owns copies of the geometry now; drop the CPU side.
        CollisionPositions.resize( Vertices.size() );
        for( size_t i = 0; i < Vertices.size(); ++i )
        {
            CollisionPositions[i] = Vertices[i].Position;
        }
        CollisionIndices = Indices;
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
