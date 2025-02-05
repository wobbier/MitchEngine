#include "PCH.h"
#include "Mesh.h"

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
#include <Primitives/Plane.h>
#include "Primitives/Cube.h"
#include "Events/EditorEvents.h"

Mesh::Mesh()
    : Component( "Mesh" )
{
    MeshMaterial = MakeShared<DiffuseMaterial>();
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

    switch( Type )
    {
    case Moonlight::Model:
        break;
    case Moonlight::Plane:
        MeshReferece = new PlaneMesh();
        break;
    case Moonlight::Cube:
        MeshReferece = new Moonlight::CubeMesh();
        break;
    case Moonlight::MeshCount:
        break;
    default:
        break;
    }
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
                }
                else
                {
                    MeshMaterial = MakeShared<DiffuseMaterial>();
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

    if( inJson.contains( "MeshType" ) )
    {
        Type = GetMeshTypeFromString( inJson["MeshType"] );
    }
    if( !MeshMaterial )
    {
        MeshMaterial = MakeShared<DiffuseMaterial>();
        MeshMaterial->Init();
    }
}

std::string Mesh::GetMeshTypeString( Moonlight::MeshType InType )
{
    switch( InType )
    {
    case Moonlight::MeshType::Plane:
        return "Plane";
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
    else
    {
        return Moonlight::MeshType::Model;
    }
}

#if USING( ME_EDITOR )

void Mesh::OnEditorInspect()
{
    if( !MeshReferece )
    {
        if( ImGui::BeginCombo( "Mesh Type", GetMeshTypeString( Type ).c_str() ) )
        {
            for( int n = 0; n < Moonlight::MeshType::MeshCount; n++ )
            {
                if( ImGui::Selectable( GetMeshTypeString( (Moonlight::MeshType)n ).c_str(), false ) )
                {
                    Type = (Moonlight::MeshType)n;

                    static_cast<RenderCore*>( GetEngine().GetWorld().lock()->GetCore( RenderCore::GetTypeId() ) )->UpdateMesh( this );

                    break;
                }
            }
            ImGui::EndCombo();
        }
    }

    if( MeshReferece )
    {
        ImGui::Text( "Vertices: %i", MeshReferece->Vertices.size() );
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
