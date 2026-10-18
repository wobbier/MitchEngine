#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>
#include <vector>

namespace Moonlight
{
    // Shared immutable topology over compatible vertex grids. CPU indices support lazy wireframe.
    class MeshIndexData
    {
    public:
        explicit MeshIndexData( std::vector<uint32_t> InIndices );
        ~MeshIndexData();
        MeshIndexData( const MeshIndexData& ) = delete;
        MeshIndexData& operator=( const MeshIndexData& ) = delete;
        bgfx::IndexBufferHandle GetBuffer() const { return m_buffer; }
        bgfx::IndexBufferHandle GetWireframe();
        uint32_t GetCount() const { return static_cast<uint32_t>( m_indices.size() ); }
    private:
        std::vector<uint32_t> m_indices;
        bgfx::IndexBufferHandle m_buffer = BGFX_INVALID_HANDLE, m_wireframe = BGFX_INVALID_HANDLE;
    };
}
