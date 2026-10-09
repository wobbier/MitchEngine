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
    Transform& parentTransform = parentEnt->GetComponent<Transform>();
    World& world = *GetEngine().GetWorld().lock();

    for( auto& childNode : root.Nodes )
    {
        // A saved scene already contains this node's entity (with any edits made to it): reuse it.
        EntityHandle entityNode;
        if( Transform* existing = parentTransform.GetChildByName( childNode.Name ) )
        {
            entityNode = existing->Parent;
        }
        else
        {
            entityNode = world.CreateEntity( childNode.Name );
            auto& transform = entityNode->AddComponent<Transform>();
            transform.SetParent( parentTransform );
            transform.SetPosition( childNode.Position );
            transform.SetScale( childNode.Scale );
            transform.SetRotation( childNode.Rotation );
        }
        RecursiveLoadMesh( childNode, entityNode );
    }

    if( root.Meshes.size() == 1 )
    {
        Mesh& meshRef = parentEnt->AddComponent<Mesh>( root.Meshes[0] );
        // AddComponent returns the deserialized Mesh when there is one; point it at the geometry.
        meshRef.MeshReferece = root.Meshes[0];
        if( root.IsFlipped )
        {
            meshRef.MeshMaterial->FaceMode = Moonlight::RenderFaceMode::Back;
        }
    }
    else
    {
        for( auto child : root.Meshes )
        {
            EntityHandle ent;
            if( Transform* existing = parentTransform.GetChildByName( child->Name ) )
            {
                ent = existing->Parent;
            }
            else
            {
                ent = world.CreateEntity( child->Name );
                Transform& transform = ent->AddComponent<Transform>();
                transform.SetParent( parentTransform );
            }
            Mesh& meshRef = ent->AddComponent<Mesh>( child );
            meshRef.MeshReferece = child;
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
        ImGui::Text("%s", ModelPath.GetLocalPath().data());
        RecursiveModelNode( ModelHandle, ModelHandle->RootNode );
        if( !ModelHandle->GetAnimations().empty() && ImGui::TreeNode( "Animations" ) )
        {
            for( const Moonlight::AnimationClip& clip : ModelHandle->GetAnimations() )
            {
                ImGui::BulletText( "%s  (%.2f s, %zu channels)", clip.Name.c_str(), clip.Duration, clip.Channels.size() );
            }
            ImGui::TreePop();
        }
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
                const AssetDescriptor* _p = AssetDescriptor::GetDragged();
                if( !_p ) return;
                const AssetDescriptor& payload_n = *_p;

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
