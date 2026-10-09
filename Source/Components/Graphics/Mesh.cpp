#include "PCH.h"
#include "Mesh.h"
#include "Model.h"
#include "Components/Transform.h"
#include <algorithm>
#include <unordered_map>

#include "Graphics/MeshData.h"
#include "Graphics/Texture.h"

#if USING( ME_EDITOR )

#include "Utils/HavanaUtils.h"
#include "imgui.h"
#include "Events/HavanaEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Cores/Rendering/RenderCore.h"

#endif
#include "RenderCommands.h"
#include "Graphics/MaterialDetail.h"
#include "CLog.h"
#include "Utils/ImGuiUtils.h"
#include <Materials/DiffuseMaterial.h>
#include "Types/AssetDescriptor.h"
#include "Primitives/Primitives.h"
#include "Events/EditorEvents.h"

Mesh::Mesh()
    : Component( "Mesh" )
{
    MeshMaterial = MakeShared<StandardMaterial>();
    MeshMaterial->Init();
}

Mesh::Mesh( Moonlight::MeshType InType, Moonlight::Material* InMaterial )
    : Component( "Mesh" )
    , Type( InType )
{
    if( InMaterial )
    {
        MeshMaterial = InMaterial->CreateInstance();
        MeshMaterial->Init();
    }

    MeshReferece = Moonlight::Primitives::Get( Type );
}

Mesh::Mesh( Moonlight::MeshData* mesh )
    : Component( "Mesh" )
    , MeshReferece( mesh )
    , Type( Moonlight::MeshType::Model )
{
    MeshMaterial = MeshReferece->MeshMaterial->CreateInstance();
    MeshMaterial->Init();
}

Mesh::~Mesh()
{
    MeshMaterial.reset();
}

void Mesh::Init()
{
    // Primitive meshes loaded from a scene only know their MeshType; they share one geometry per type.
    if( !MeshReferece )
    {
        MeshReferece = Moonlight::Primitives::Get( Type );
    }
}

unsigned int Mesh::GetId()
{
    return Id;
}

Moonlight::MeshType Mesh::GetType() const
{
    return Type;
}

void Mesh::OnSerialize( json& outJson )
{
    if( MeshMaterial )
    {
        json& mat = outJson["Material"];
        MeshMaterial->OnSerialize( mat );
    }
    outJson["MeshType"] = GetMeshTypeString( Type );
    outJson["CastShadows"] = CastShadows;
}

void Mesh::OnDeserialize( const json& inJson )
{
    if( !MeshMaterial )
    {

    }
    if( MeshMaterial )
    {
        // For fixing old scenes, delete this once you update your scenes
        if( inJson.contains( "Material" ) && inJson["Material"].contains( "Type" ) )
        {
            const std::string& matType = inJson["Material"]["Type"];
            if( MeshMaterial->GetTypeName() != matType )
            {
                MeshMaterial.reset();
                MaterialRegistry& reg = GetMaterialRegistry();
                if( reg.find( matType ) != reg.end() )
                {
                    MeshMaterial = reg[matType].CreateFunc();
                    MeshMaterial->Init();
                }
                else
                {
                    MeshMaterial = MakeShared<StandardMaterial>();
                    MeshMaterial->Init();
                }
            }
            MeshMaterial->OnDeserialize( inJson["Material"] );
        }
        else
        {
            MeshMaterial->OnDeserialize( inJson );
        }
    }
    else
    {
        BRUH( "Material isn't created in OnDeserialize" );
    }

    if( inJson.contains( "CastShadows" ) && inJson["CastShadows"].is_boolean() )
    {
        CastShadows = inJson["CastShadows"].get<bool>();
    }
    if( inJson.contains( "MeshType" ) )
    {
        const Moonlight::MeshType newType = GetMeshTypeFromString( inJson["MeshType"] );
        // Switching between primitive shapes (e.g. undoing a shape change) swaps the shared geometry.
        if( newType != Type && Moonlight::Primitives::IsPrimitive( newType ) && ( !MeshReferece || Moonlight::Primitives::IsPrimitive( Type ) ) )
        {
            MeshReferece = MeshReferece ? Moonlight::Primitives::Get( newType ) : nullptr;
        }
        Type = newType;
    }
    if( !MeshMaterial )
    {
        MeshMaterial = MakeShared<StandardMaterial>();
        MeshMaterial->Init();
    }
}

std::string Mesh::GetMeshTypeString( Moonlight::MeshType InType )
{
    switch( InType )
    {
    case Moonlight::MeshType::Plane:
        return "Plane";
    case Moonlight::MeshType::Cube:
        return "Cube";
    case Moonlight::MeshType::Sphere:
        return "Sphere";
    case Moonlight::MeshType::Cylinder:
        return "Cylinder";
    case Moonlight::MeshType::Capsule:
        return "Capsule";
    case Moonlight::MeshType::Model:
    default:
        return "Model";
    }
}

Moonlight::MeshType Mesh::GetMeshTypeFromString( const std::string& InType )
{
    if( InType == "Plane" )
    {
        return Moonlight::MeshType::Plane;
    }
    else if( InType == "Cube" )
    {
        return Moonlight::MeshType::Cube;
    }
    else if( InType == "Sphere" )
    {
        return Moonlight::MeshType::Sphere;
    }
    else if( InType == "Cylinder" )
    {
        return Moonlight::MeshType::Cylinder;
    }
    else if( InType == "Capsule" )
    {
        return Moonlight::MeshType::Capsule;
    }
    else
    {
        return Moonlight::MeshType::Model;
    }
}

#if USING( ME_EDITOR )

void Mesh::OnEditorInspect()
{
    // Primitive meshes can switch shape; model meshes keep their imported geometry.
    if( Moonlight::Primitives::IsPrimitive( Type ) )
    {
        HavanaUtils::Label( "Shape" );
        if( ImGui::BeginCombo( "##Shape", GetMeshTypeString( Type ).c_str() ) )
        {
            for( int n = 0; n < Moonlight::MeshType::MeshCount; n++ )
            {
                const Moonlight::MeshType shape = static_cast<Moonlight::MeshType>( n );
                if( !Moonlight::Primitives::IsPrimitive( shape ) )
                {
                    continue;
                }
                if( ImGui::Selectable( GetMeshTypeString( shape ).c_str(), shape == Type ) )
                {
                    Type = shape;
                    MeshReferece = Moonlight::Primitives::Get( shape );
                }
            }
            ImGui::EndCombo();
        }
    }

    HavanaUtils::Label( "Cast Shadows" );
    ImGui::Checkbox( "##CastShadows", &CastShadows );

    if( MeshReferece )
    {
        ImGui::Text( "Vertices: %u", MeshReferece->GetVertexCount() );
    }


    ImGui::Text( "" );

    std::map<std::string, MaterialTest> folders;
    MaterialRegistry& reg = GetMaterialRegistry();

    for( auto& thing : reg )
    {
        if( thing.second.Folder == "" )
        {
            folders[""].Reg[thing.first] = &thing.second;
        }
        else
        {
            /*auto it = folders.at(thing.second.Folder);
            if (it == folders.end())
            {

            }*/
            std::string folderPath = thing.second.Folder;
            std::size_t pos = folderPath.rfind( "/" );
            if( pos == std::string::npos )
            {
                folders[thing.second.Folder].Reg[thing.first] = &thing.second;
            }
            else
            {
                MaterialTest& test = folders[thing.second.Folder.substr( 0, pos )];
                while( pos != std::string::npos )
                {
                    pos = folderPath.rfind( "/" );
                    if( pos == std::string::npos )
                    {
                        test.Folders[folderPath].Reg[thing.first] = &thing.second;
                    }
                    else
                    {
                        test = folders[folderPath.substr( 0, pos )];
                        folderPath = folderPath.substr( pos + 1, folderPath.size() );
                    }
                }
            }
        }
    }

    // #TODO: Move this to the material class, or an inspector type...
    // Also while I'm here, material properties should be saved in a different file for re-use.
    // Maybe an "instance"?
    HavanaUtils::Label( "Material Type" );
    if( ImGui::BeginCombo( "##Material Type", ( MeshMaterial ) ? reg[MeshMaterial->GetTypeName()].Name.c_str() : "Selected Material" ) )
    {
        for( auto& thing : folders )
        {
            if( thing.first != "" )
            {
                if( ImGui::BeginMenu( thing.first.c_str() ) )
                {
                    DoMaterialRecursive( thing.second );
                    ImGui::EndMenu();
                }
            }
            else
            {
                for( auto& ptr : thing.second.Reg )
                {
                    SelectMaterial( ptr, reg );
                }
            }
        }
        ImGui::EndCombo();
    }

    if( MeshMaterial )
    {
        if( ImGui::Button( "Select Shader" ) )
        {
            RequestAssetSelectionEvent evt( [this]( Path selectedAsset )
                {
                    std::string path = selectedAsset.GetLocalPathString();
                    size_t pos = path.rfind( selectedAsset.GetExtension() );
                    if( pos != std::string::npos )
                    {
                        path.erase( pos - 1, path.length() );
                    }
                    MeshMaterial->LoadShader( path );
                }, AssetType::ShaderGraph );
            evt.Fire();
        }

        if( MeshMaterial->MeshShader.IsLoaded() && ImGui::Button( "Reload Shader" ) )
        {
            MeshMaterial->LoadShader( MeshMaterial->ShaderName );
        }
        if( ImGui::Button("Save As Material File: ") )
        {
            RequestAssetSelectionEvent evt( [this]( const Path& inPath )
                {
                    json outJson;
                    OnSerialize( outJson );
                    File( inPath ).Write( outJson.dump( 1 ) );
                }, AssetType::Material, true );
            evt.Fire();
        }
        bool shouldClose = true;
        if( ImGui::CollapsingHeader( "Material Properties", &shouldClose, ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            MeshMaterial->OnEditorInspect();
        }
    }
}

void Mesh::DoMaterialRecursive( const MaterialTest& currentFolder )
{
    for( auto& entry : currentFolder.Folders )
    {
        if( ImGui::BeginMenu( entry.first.c_str() ) )
        {
            DoMaterialRecursive( entry.second );
            ImGui::EndMenu();
        }
    }
    for( auto& ptr : currentFolder.Reg )
    {
        SelectMaterial( ptr, GetMaterialRegistry() );
    }
}

void Mesh::SelectMaterial( const std::pair<std::string, MaterialInfo*>& ptr, MaterialRegistry& reg )
{
    if( ImGui::Selectable( ptr.second->Name.c_str() ) && MeshMaterial && MeshMaterial->GetTypeName() != ptr.first )
    {
        std::vector<SharedPtr<Moonlight::Texture>> textures = MeshMaterial->GetTextures();
        Vector2 tiling = MeshMaterial->Tiling;
        Vector3 diffuse = MeshMaterial->DiffuseColor;
        MeshMaterial.reset();

        MeshMaterial = reg[ptr.first].CreateFunc();
        MeshMaterial->Init();

        //textures
        for( int i = 0; i < Moonlight::TextureType::Count; ++i )
        {
            MeshMaterial->SetTexture( (Moonlight::TextureType)i, textures[i] );
        }
        MeshMaterial->DiffuseColor = diffuse;
        MeshMaterial->Tiling = tiling;

        static_cast<RenderCore*>( GetEngine().GetWorld().lock()->GetCore( RenderCore::GetTypeId() ) )->UpdateMesh( this );
    }
}
#endif


bool Mesh::IsSkinned() const
{
    return MeshReferece && MeshReferece->IsSkinned();
}


bool Mesh::ResolveBones()
{
    m_bones.clear();
    if( !IsSkinned() || !Parent )
    {
        return false;
    }
    // Bones live below the Model this mesh came from (the nearest ancestor with a Model).
    Transform* root = Parent->TryGetComponent<Transform>();
    for( Transform* node = root; node; node = node->GetParentTransform() )
    {
        root = node;
        if( node->Parent && node->Parent->HasComponent<Model>() )
        {
            break;
        }
    }
    if( !root )
    {
        return false;
    }
    std::unordered_map<std::string, Transform*> nodes;
    std::vector<Transform*> stack{ root };
    while( !stack.empty() )
    {
        Transform* node = stack.back();
        stack.pop_back();
        nodes.emplace( node->GetName(), node );
        for( Transform* child : node->GetChildren() )
        {
            if( child )
            {
                stack.push_back( child );
            }
        }
    }
    bool complete = true;
    m_bones.reserve( MeshReferece->BoneNames.size() );
    for( const std::string& name : MeshReferece->BoneNames )
    {
        auto found = nodes.find( name );
        if( found != nodes.end() )
        {
            m_bones.push_back( found->second->Parent );
        }
        else
        {
            // A bone collapsed into the Model entity on import (a single root node): use the root.
            m_bones.push_back( root->Parent );
            complete = complete && root->Parent;
        }
    }
    return complete;
}


AABB Mesh::UpdateSkin( const glm::mat4& InMeshWorld )
{
    const size_t count = MeshReferece->BoneNames.size();
    bool valid = m_bonesResolved && m_bones.size() == count;
    for( size_t i = 0; valid && i < count; ++i )
    {
        valid = static_cast<bool>( m_bones[i] );
    }
    if( !valid )
    {
        m_bonesResolved = ResolveBones();
    }

    const glm::mat4 meshInverse = glm::inverse( InMeshWorld );
    m_skinPalette.resize( count );
    AABB bounds;
    for( size_t i = 0; i < count; ++i )
    {
        Transform* bone = i < m_bones.size() && m_bones[i] ? m_bones[i]->TryGetComponent<Transform>() : nullptr;
        const glm::mat4 boneWorld = bone ? bone->GetLocalToWorldMatrix().GetInternalMatrix() : InMeshWorld * glm::inverse( MeshReferece->BoneOffsets[i] );
        m_skinPalette[i] = meshInverse * boneWorld * MeshReferece->BoneOffsets[i];
        // Every vertex a bone moves stays within its radius of the bone.
        const float scale = std::max( { glm::length( glm::vec3( boneWorld[0] ) ), glm::length( glm::vec3( boneWorld[1] ) ), glm::length( glm::vec3( boneWorld[2] ) ) } );
        const float radius = MeshReferece->BoneRadii[i] * scale;
        const Vector3 center( boneWorld[3].x, boneWorld[3].y, boneWorld[3].z );
        bounds.Encapsulate( center - Vector3( radius, radius, radius ) );
        bounds.Encapsulate( center + Vector3( radius, radius, radius ) );
    }
    // Unweighted vertices stay rigid with the mesh.
    m_skinBounds = bounds.IsValid() ? bounds : MeshReferece->Bounds.Transformed( Matrix4( InMeshWorld ) );
    return m_skinBounds;
}


AABB Mesh::GetWorldBounds( const Matrix4& InWorld ) const
{
    if( IsSkinned() && m_skinBounds.IsValid() )
    {
        return m_skinBounds;
    }
    return MeshReferece && MeshReferece->Bounds.IsValid() ? MeshReferece->Bounds.Transformed( InWorld ) : AABB();
}
