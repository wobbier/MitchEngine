#pragma once
#include <string>
#include <vector>

#include <Graphics/ShaderStructures.h>
#include <Pointers.h>
#include "Math/Bounds.h"
#include "Graphics/MeshLod.h"
#include "Graphics/MeshIndexData.h"
#include <atomic>
#include <memory>
#include <bgfx/bgfx.h>
#include <glm/mat4x4.hpp>
#include "Graphics/VertexAnimation.h"

namespace Moonlight
{
    class Material;

    class MeshData
    {
        friend class BGFXRenderer;
    public:
        MeshData() = default;
        MeshData( std::vector<PosNormTexTanBiVertex> vertices, std::vector<uint32_t> indices, SharedPtr<Material> newMaterial = nullptr );
        virtual ~MeshData();

        void Draw( SharedPtr<Material> inMaterial );

        // CPU-side geometry, only valid until InitMesh() uploads it to the GPU.
        std::vector<PosNormTexTanBiVertex> Vertices;
        std::vector<uint32_t> Indices;

        // Local-space bounds, computed from the vertices when the GPU buffers are created.
        AABB Bounds;
        // Positions and triangle indices kept on the CPU after upload, for mesh colliders.
        std::vector<Vector3> CollisionPositions;
        std::vector<uint32_t> CollisionIndices;
        SharedPtr<Moonlight::Material> MeshMaterial;

        const bgfx::VertexBufferHandle& GetVertexBuffer() const {
            return m_vbh;
        }
        const bgfx::IndexBufferHandle& GetIndexuffer() const {
            return m_ibh;
        }
        // A baked animation instance supplies its posed buffer to every render pass. The wind stream
        // rides along as stream 1 (skinned draws replace it with their weights afterwards).
        void BindVertexBuffer( bgfx::DynamicVertexBufferHandle InPosed ) const {
            if( bgfx::isValid( InPosed ) ) bgfx::setVertexBuffer( 0, InPosed );
            else bgfx::setVertexBuffer( 0, m_vbh );
            if( bgfx::isValid( m_windVbh ) ) bgfx::setVertexBuffer( 1, m_windVbh );
        }

        // Generated trees: one WindVertex per vertex (vertex buffer stream 1, see FoliageMaterial).
        void InitWindStream( const std::vector<WindVertex>& InWind );
        bool HasWindStream() const {
            return bgfx::isValid( m_windVbh );
        }

        // Skinning. Bone i deforms vertices through inverse(meshWorld) * boneWorld * BoneOffsets[i]
        // (the offset maps bind-pose mesh space into the bone's space). Bones are found by name in
        // the model's node entities.
        static constexpr uint32_t kMaxBones = 128;
        std::vector<std::string> BoneNames;
        std::vector<glm::mat4> BoneOffsets;
        std::vector<float> BoneRadii;   // furthest influenced vertex from each bone, for skinned bounds
        // Baked animation shares immutable positions/templates; instances own posed buffers.
        SharedPtr<VertexAnimation> VertexCache;
        std::vector<uint32_t> VertexCacheIndices;
        // Normals/tangents are expressed in the cached vertex's bind frame, once on import.
        std::vector<PosNormTexTanBiVertex> VertexCacheTemplate;
        bool IsSkinned() const {
            return bgfx::isValid( m_skinVbh );
        }
        const bgfx::VertexBufferHandle& GetSkinBuffer() const {
            return m_skinVbh;
        }
        // Uploads one SkinWeightsVertex per vertex (vertex buffer stream 1).
        void InitSkin( const std::vector<SkinWeightsVertex>& InWeights );
        uint32_t GetVertexCount() const {
            return m_vertexCount;
        }
        uint32_t GetIndexCount() const {
            return m_topology ? m_topology->GetCount() : m_indexCount;
        }

        // Levels of detail (a model's import settings): index buffers over this mesh's vertices,
        // coarser each level, each drawn below a relative screen height. Level 0 is the full mesh.
        void SetLods( const std::vector<MeshLod>& InLods );
        // Overrides visual topology in every pass; canonical CPU collision remains unchanged.
        void SetIndexTopology( std::shared_ptr<MeshIndexData> InTopology ) { m_topology = std::move( InTopology ); }
        // Simplified levels (not counting the full mesh).
        uint8_t GetLodCount() const {
            return static_cast<uint8_t>( m_lodIbh.size() );
        }
        const float* GetLodHeights() const {
            return m_lodHeights.data();
        }
        bgfx::IndexBufferHandle GetIndexBuffer( uint8_t InLod ) const {
            return m_topology ? m_topology->GetBuffer() : ( InLod == 0 || InLod > m_lodIbh.size() ? m_ibh : m_lodIbh[InLod - 1] );
        }
        // Triangle edges, uploaded on first use. Reuses the same static/posed/skinned vertices.
        bgfx::IndexBufferHandle GetWireframeIndexBuffer( uint8_t InLod );
        uint32_t GetIndexCount( uint8_t InLod ) const {
            return m_topology ? m_topology->GetCount() : ( InLod == 0 || InLod > m_lodIndexCounts.size() ? m_indexCount : m_lodIndexCounts[InLod - 1] );
        }

// render the mesh
//void Draw(SharedPtr<Material> mat, ID3D11DeviceContext* context, bool depthOnly = false);
        std::string Name;

        // Vertex and index buffer bytes of every live mesh and shared topology (hardware budgets).
        // Wireframe views are inspection-only and not counted.
        static int64_t GetTotalGpuBytes() { return s_totalGpuBytes.load( std::memory_order_relaxed ); }
        static void TrackGpuBytes( int64_t InBytes ) { s_totalGpuBytes.fetch_add( InBytes, std::memory_order_relaxed ); }

    protected:
        void InitMesh();

    private:
        void AddGpuBytes( int64_t InBytes ) { m_gpuBytes += InBytes; TrackGpuBytes( InBytes ); }

        static inline std::atomic<int64_t> s_totalGpuBytes{ 0 };
        int64_t m_gpuBytes = 0;
        std::shared_ptr<MeshIndexData> m_topology;
        uint32_t m_vertexCount = 0;
        uint32_t m_indexCount = 0;
        bgfx::VertexBufferHandle m_vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle m_ibh = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle m_skinVbh = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle m_windVbh = BGFX_INVALID_HANDLE;
        std::vector<bgfx::IndexBufferHandle> m_lodIbh;
        std::vector<std::vector<uint32_t>> m_lodIndices;
        std::vector<bgfx::IndexBufferHandle> m_wireframeIbh;
        std::vector<uint32_t> m_lodIndexCounts;
        std::vector<float> m_lodHeights;
    };
}
