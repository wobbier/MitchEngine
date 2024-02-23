#pragma once
#include <vector>

namespace Moonlight {
    class MeshData;
}

namespace Moonlight
{
    class Node
    {
    public:
        Node() {}
        std::vector<Node> Nodes;
        std::vector<Moonlight::MeshData*> Meshes;
        std::string Name;
        Vector3 Position;
    };
}