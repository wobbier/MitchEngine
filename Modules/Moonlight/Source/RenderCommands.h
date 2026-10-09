#pragma once
#include <vector>
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "Math/Matrix4.h"
#include <string>
#include <cstdint>
#include "Pointers.h"

namespace Moonlight {
    class ShaderCommand;
}

namespace Moonlight {
    class MeshData;
}
namespace Moonlight {
    class Material;
}
namespace Moonlight {
    class SkyBox;
}

namespace Moonlight
{
    enum MeshType
    {
        Model = 0,
        Plane,
        Cube,
        Sphere,
        Cylinder,
        Capsule,
        MeshCount
    };

    struct ModelCommand
    {
        std::vector<MeshData*> Meshes;
        ShaderCommand* ModelShader;
        Matrix4 Transform;
    };

    struct DebugColliderCommand
    {
        MeshType Type = MeshType::MeshCount;
        glm::mat4 Transform;
    };

    struct MeshCommand
    {
        MeshCommand() = default;
        void Reset()
        {
            SingleMesh = nullptr;
            MeshMaterial = nullptr;
            Transform = glm::mat4( 1.f );
            ID = 0u;
        }
        MeshData* SingleMesh = nullptr;
        SharedPtr<Material> MeshMaterial = nullptr;
        glm::mat4 Transform;
        MeshType Type = MeshType::Model;
        uint64_t ID = 0u;
        int VisibilityIndex = 0;
        bool Visible = false;

        uint64_t BatchKey = 0u;
        uint16_t VertexBufferIdx = UINT16_MAX;
        uint16_t IndexBufferIdx = UINT16_MAX;
        bool IsTransparent = false;
        bool SupportsInstancing = false;
        bool CastShadows = true;
    };

    enum class LightType : uint8_t
    {
        Directional = 0,
        Point,
        Spot,
    };

    // One light for this frame, gathered from Light components by RenderCore.
    struct LightCommand
    {
        LightType Type = LightType::Point;
        Vector3 Position;
        Vector3 Direction = Vector3( 0.f, -1.f, 0.f );   // world space, where the light points
        Vector3 Color = Vector3( 1.f, 1.f, 1.f );         // linear RGB * intensity
        float Range = 10.f;
        float CosInner = 0.9f;
        float CosOuter = 0.8f;
        bool CastShadows = false;
        float ShadowBias = 0.0005f;
        float ShadowNormalBias = 0.02f;
    };
}