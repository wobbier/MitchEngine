#pragma once
#include <vector>
#include <string>
#include "Graphics/MeshData.h"
#include "Pointers.h"

namespace Moonlight { class Material; }


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

        // FBX material splits often have empty or repeated mesh names. Each split needs its own
        // entity, including after saving and reopening the scene.
        std::string GetMeshEntityName( size_t InIndex ) const
        {
            const std::string& name = Meshes[InIndex]->Name;
            bool unique = !name.empty();
            for( size_t i = 0; i < Meshes.size(); ++i )
            {
                if( i != InIndex && Meshes[i]->Name == name ) unique = false;
            }
            for( const Node& node : Nodes )
            {
                if( node.Name == name ) unique = false;
            }
            if( unique ) return name;

            std::string generated = Name + "_Mesh_" + std::to_string( InIndex );
            for( ;; generated += '_' )
            {
                bool taken = false;
                for( const auto* mesh : Meshes ) taken = taken || mesh->Name == generated;
                for( const Node& node : Nodes ) taken = taken || node.Name == generated;
                if( !taken ) return generated;
            }
        }
    };
}
