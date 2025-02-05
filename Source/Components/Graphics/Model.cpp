#include "PCH.h"
#include "Model.h"
#include "Graphics/ModelResource.h"
#include "Resource/ResourceCache.h"
#include "Components/Transform.h"
#include "Mesh.h"
#include "Engine/Engine.h"
#include "ECS/Entity.h"
#include "Events/HavanaEvents.h"
#include "Types/AssetDescriptor.h"
#include "Events/EditorEvents.h"

Model::Model( const std::string& path )
    : Component( "Model" )
    , ModelPath( path )
{
}

Model::Model()
    : Component( "Model" )
{
}

Model::~Model()
{
}

void Model::Init()
{
    OPTICK_EVENT( "Model::Init" );
    if( !ModelPath.FullPath.empty() )
    {
        ModelHandle = ResourceCache::GetInstance().Get<ModelResource>( ModelPath );
    }

    if( ModelHandle && !IsInitialized )
    {
        IsInitialized = true;
        ME_ASSERT_MSG( ModelHandle->RootNode.Meshes.empty(), "you can have a mesh on the root??" );
        if(ModelHandle->RootNode.Nodes.size() == 1 )
        {
            RecursiveLoadMesh( ModelHandle->RootNode.Nodes[0], Parent);
        }
        else
        {
            RecursiveLoadMesh( ModelHandle->RootNode, Parent );
        }
    }
}

void Model::RecursiveLoadMesh( Moonlight::Node& root, EntityHandle& parentEnt )
{
    for( auto& childNode : root.Nodes )
    {
        EntityHandle entityNode = GetEngine().GetWorld().lock()->CreateEntity();
        auto& transform = entityNode->AddComponent<Transform>( childNode.Name );
        transform.SetParent( parentEnt->GetComponent<Transform>() );
        transform.SetPosition( childNode.Position );
        transform.SetScale( childNode.Scale );
        transform.SetRotation( childNode.Rotation );
        RecursiveLoadMesh( childNode, entityNode );
    }

    if( root.Meshes.size() == 1 )
    {
        auto& meshRef = parentEnt->AddComponent<Mesh>( root.Meshes[0] );
        meshRef.MeshReferece = root.Meshes[0];
    }
    else
    {
        for( auto child : root.Meshes )
        {
            auto ent = GetEngine().GetWorld().lock()->CreateEntity();
            Transform& trans = ent->AddComponent<Transform>( child->Name );
            Mesh& meshRef = ent->AddComponent<Mesh>( child );
            meshRef.MeshReferece = child;
            //trans.SetPosition( root.Position );
            //trans.SetScale( root.Scale );
            //trans.SetRotation( root.Rotation );
            trans.SetParent( parentEnt->GetComponent<Transform>() );
        }
    }
}

#if USING( ME_EDITOR )

void RecursiveModelNode( SharedPtr<ModelResource>& inModel, Moonlight::Node& parent )
{
    if( ImGui::CollapsingHeader( parent.Name.c_str(), 0 /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
    {
        HavanaUtils::EditableVector3( "Position", parent.Position );
        HavanaUtils::EditableVector3( "Rotaion", parent.EulerRotation );
        for( auto& child : parent.Nodes )
        {
            RecursiveModelNode( inModel, child );
        }
    }
}

void Model::OnEditorInspect()
{
    if( ModelHandle )
    {
        ImGui::Text( "Loaded Path" );
        ImGui::SameLine();
        ImGui::Text( ModelPath.GetLocalPath().data() );
        RecursiveModelNode( ModelHandle, ModelHandle->RootNode );
    }
    else
    {
        ImVec2 selectorSize( -1.f, 19.f );
        HavanaUtils::Label( "Model" );
        if( ImGui::Button( ( ( !ModelPath.GetLocalPath().empty() ) ? ModelPath.GetLocalPath().data() : "Select Asset" ), selectorSize ) )
        {
            RequestAssetSelectionEvent evt( [this]( Path selectedAsset )
                {
                    ModelPath = selectedAsset;
                    Init();
                }, AssetType::Model );
            evt.Fire();
        }

        if( ImGui::BeginDragDropTarget() )
        {
            if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( AssetDescriptor::kDragAndDropPayload ) )
            {
                IM_ASSERT( payload->DataSize == sizeof( AssetDescriptor ) );
                const AssetDescriptor& payload_n = *static_cast<AssetDescriptor*>( payload->Data );

                if( payload_n.Type == AssetType::Model )
                {
                    ModelPath = payload_n.FullPath;
                    Init();
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
}

#endif
