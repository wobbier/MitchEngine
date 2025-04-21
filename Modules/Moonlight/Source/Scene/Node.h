#pragma once
#include <vector>
#include "Graphics/MeshData.h"
#include "Pointers.h"

namespace Moonlight { class Material; }

DISABLE_OPTIMIZATION;

namespace Moonlight
{
    class Node
    {
    public:
        Node() {}
        ~Node()
        {
        }
        std::vector<Node> Nodes;
        std::vector<Moonlight::MeshData*> Meshes;
        std::vector<std::string> UnresolvedTextureNames;
        std::vector<SharedPtr<Moonlight::Material>> MaterialCache;
        std::string Name;
        Vector3 Position;
        Vector3 Scale = { 1.f, 1.f, 1.f };
        Vector3 EulerRotation;
        Quaternion Rotation;
        Matrix4 NodeMatrix;
        bool IsFlipped = false;
    };
}
