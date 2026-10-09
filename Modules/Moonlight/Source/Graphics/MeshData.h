#pragma once
#include <string>
#include <vector>

#include <Graphics/ShaderStructures.h>
#include <Pointers.h>
#include "Math/Bounds.h"
#include <bgfx/bgfx.h>
#include <glm/mat4x4.hpp>

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

        // Skinning. Bone i deforms vertices through inverse(meshWorld) * boneWorld * BoneOffsets[i]
        // (the offset maps bind-pose mesh space into the bone's space). Bones are found by name in
        // the model's node entities.
        static constexpr uint32_t kMaxBones = 128;
        std::vector<std::string> BoneNames;
        std::vector<glm::mat4> BoneOffsets;
        std::vector<float> BoneRadii;   // furthest influenced vertex from each bone, for skinned bounds
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
            return m_indexCount;
        }

// render the mesh
//void Draw(SharedPtr<Material> mat, ID3D11DeviceContext* context, bool depthOnly = false);
        std::string Name;

    protected:
        void InitMesh();

    private:
        uint32_t m_vertexCount = 0;
        uint32_t m_indexCount = 0;
        bgfx::VertexBufferHandle m_vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle m_ibh = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle m_skinVbh = BGFX_INVALID_HANDLE;
    };
}
