#include "MeshIndexData.h"
#include "Utils/BGFXUtils.h"
#include <utility>

namespace Moonlight
{
    MeshIndexData::MeshIndexData( std::vector<uint32_t> indices ) : m_indices( std::move( indices ) )
    {
        if( IsGpuAlive() && !m_indices.empty() )
            m_buffer = bgfx::createIndexBuffer( bgfx::copy( m_indices.data(), static_cast<uint32_t>( m_indices.size() * sizeof( uint32_t ) ) ), BGFX_BUFFER_INDEX32 );
    }


    MeshIndexData::~MeshIndexData()
    {
        if( !IsGpuAlive() ) return;
        if( bgfx::isValid( m_buffer ) ) bgfx::destroy( m_buffer );
        if( bgfx::isValid( m_wireframe ) ) bgfx::destroy( m_wireframe );
    }


    bgfx::IndexBufferHandle MeshIndexData::GetWireframe()
    {
        if( !IsGpuAlive() || bgfx::isValid( m_wireframe ) || m_indices.empty() ) return m_wireframe;
        std::vector<uint32_t> lines; lines.reserve( m_indices.size() * 2 );
        for( size_t i = 0; i + 2 < m_indices.size(); i += 3 )
        {
            const auto a = m_indices[i], b = m_indices[i + 1], c = m_indices[i + 2];
            lines.insert( lines.end(), { a, b, b, c, c, a } );
        }
        m_wireframe = bgfx::createIndexBuffer( bgfx::copy( lines.data(), static_cast<uint32_t>( lines.size() * sizeof( uint32_t ) ) ), BGFX_BUFFER_INDEX32 );
        return m_wireframe;
    }
}
