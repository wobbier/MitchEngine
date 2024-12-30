#pragma once
#include <vector>
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "Math/Matrix4.h"
#include <string>
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
    };

    struct LightCommand
    {
        float test;
        /*DirectX::XMFLOAT4 pos;
        DirectX::XMFLOAT4 dir;
        DirectX::XMFLOAT4 cameraPos;
        DirectX::XMFLOAT4 ambient;
        DirectX::XMFLOAT4 diffuse;*/
    };
}