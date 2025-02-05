#pragma once
#include <string>
#include <vector>

#include <Graphics/ShaderStructures.h>
#include <Pointers.h>
#include <bgfx/bgfx.h>

namespace Moonlight
{
    class Material;

    class MeshData
    {
        friend class BGFXRenderer;
    public:
        MeshData() = default;
        MeshData( std::vector<PosNormTexTanBiVertex> vertices, std::vector<uint16_t> indices, SharedPtr<Material> newMaterial = nullptr );
        ~MeshData();

        void Draw( SharedPtr<Material> inMaterial );

        std::vector<PosNormTexTanBiVertex> Vertices;
        std::vector<uint16_t> Indices;
        SharedPtr<Moonlight::Material> MeshMaterial;

        const bgfx::VertexBufferHandle& GetVertexBuffer() const {
            return m_vbh;
        }
        const bgfx::IndexBufferHandle& GetIndexuffer() const {
            return m_ibh;
        }

// render the mesh
//void Draw(SharedPtr<Material> mat, ID3D11DeviceContext* context, bool depthOnly = false);
        std::string Name;

    protected:
        void InitMesh();

    private:
        unsigned int m_indexCount;
        bgfx::VertexBufferHandle m_vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle m_ibh = BGFX_INVALID_HANDLE;
    };
}
