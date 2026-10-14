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
#include <algorithm>
#include <cmath>

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

namespace
{
    std::vector<EntityHandle>& PendingModels()
    {
        static std::vector<EntityHandle> pending;
        return pending;
    }

    bool IsIdentity( const Moonlight::Node& InNode )
    {
        return InNode.Position.LengthSquared() < 1e-12f
            && ( InNode.Scale - Vector3( 1.f, 1.f, 1.f ) ).LengthSquared() < 1e-12f
            && InNode.Rotation.x * InNode.Rotation.x + InNode.Rotation.y * InNode.Rotation.y + InNode.Rotation.z * InNode.Rotation.z < 1e-12f
            && std::abs( std::abs( InNode.Rotation.w ) - 1.f ) < 1e-6f;
    }

    // Older scenes don't remember their cook's scale. Only repair a centimetre/metre mismatch
    // when several saved translations agree; isolated edits must not trigger a conversion.
    void CollectLegacyScales( const Moonlight::Node& InNode, const Transform& InParent, std::vector<float>& OutScales )
    {
        for( const Moonlight::Node& child : InNode.Nodes )
        {
            if( Transform* existing = InParent.GetChildByName( child.Name ) )
            {
                const glm::vec3 saved = existing->GetPosition().InternalVector;
                const glm::vec3 imported = child.Position.InternalVector;
                const float lengthSquared = glm::dot( saved, saved );
                if( lengthSquared > 1e-10f && glm::dot( imported, imported ) > 1e-10f )
                {
                    const float ratio = glm::dot( saved, imported ) / lengthSquared;
                    if( ratio > 0.f && std::isfinite( ratio )
                        && glm::length( saved * ratio - imported ) <= 0.001f * glm::length( imported ) )
                    {
                        OutScales.push_back( ratio );
                    }
                }
                CollectLegacyScales( child, *existing, OutScales );
            }
        }
    }

    float LegacyPositionScale( const Moonlight::Node& InNode, const Transform& InParent )
    {
        std::vector<float> scales;
        CollectLegacyScales( InNode, InParent, scales );
        if( scales.size() < 2 ) return 1.f;
        std::sort( scales.begin(), scales.end() );
        const float median = scales[scales.size() / 2];
        const size_t agreeing = std::count_if( scales.begin(), scales.end(), [median]( float value ) { return std::abs( value / median - 1.f ) < 0.001f; } );
        const bool unitMismatch = std::abs( median / 0.01f - 1.f ) < 0.001f || std::abs( median / 100.f - 1.f ) < 0.001f;
        return unitMismatch && agreeing >= 2 && agreeing * 4 >= scales.size() * 3 ? median : 1.f;
    }
}


void Model::Init()
{
    OPTICK_EVENT( "Model::Init" );
    if( !ModelPath.FullPath.empty() )
    {
        ModelHandle = ResourceCache::GetInstance().GetAsync<ModelResource>( ModelPath );
    }
    if( ModelHandle && ModelHandle->IsLoading() )
    {
        PendingModels().push_back( Parent );
        return;
    }
    Expand();
}


void Model::ExpandPendingModels()
{
    std::vector<EntityHandle>& pending = PendingModels();
    for( size_t i = 0; i < pending.size(); )
    {
        Model* model = pending[i] ? pending[i]->TryGetComponent<Model>() : nullptr;
        if( model && model->ModelHandle && model->ModelHandle->IsLoading() )
        {
            ++i;
            continue;
        }
        if( model && model->ModelHandle && !model->ModelHandle->HasLoadFailed() )
        {
            model->Expand();
        }
        pending.erase( pending.begin() + i );
    }
}


void Model::Expand()
{
    if( ModelHandle && !IsInitialized && !ModelHandle->HasLoadFailed() )
    {
        IsInitialized = true;
        Moonlight::Node* root = &ModelHandle->RootNode;
        // Keep the legacy collapsed layout only for identity wrappers with no animated name.
        // Dropping a translated/scaled child discards FBX pivots and import transforms.
        if( root->Meshes.empty() && root->Nodes.size() == 1 && IsIdentity( *root ) && IsIdentity( root->Nodes[0] )
            && ModelHandle->GetAnimations().empty() )
        {
            root = &root->Nodes[0];
        }
        float positionScale = 1.f;
        const float bakedScale = ModelHandle->GetBakedImportScale();
        if( bakedScale > 0.f && std::isfinite( bakedScale ) && m_hasBakedImportScale && m_bakedImportScale > 0.f && std::isfinite( m_bakedImportScale ) )
        {
            positionScale = bakedScale / m_bakedImportScale;
        }
        else positionScale = LegacyPositionScale( *root, Parent->GetComponent<Transform>() );

        if( !IsIdentity( *root ) || Parent->GetComponent<Transform>().GetChildByName( root->Name ) )
        {
            World& world = *Parent.GetWorld();
            Transform& parentTransform = Parent->GetComponent<Transform>();
            Transform* importedRoot = parentTransform.GetChildByName( root->Name );
            if( !importedRoot )
            {
                EntityHandle entity = world.CreateEntity( root->Name );
                importedRoot = &entity->AddComponent<Transform>();
                importedRoot->SetParent( parentTransform );
                importedRoot->SetPosition( root->Position );
                importedRoot->SetScale( root->Scale );
                importedRoot->SetRotation( root->Rotation );
                // Old scenes expanded the root's children directly under the Model.
                for( const Moonlight::Node& child : root->Nodes )
                {
                    if( Transform* existing = parentTransform.GetChildByName( child.Name ) ) existing->SetParent( *importedRoot );
                }
            }
            else importedRoot->SetPosition( importedRoot->GetPosition() * positionScale );
            EntityHandle entity = importedRoot->Parent;
            RecursiveLoadMesh( *root, entity, positionScale );
        }
        else RecursiveLoadMesh( *root, Parent, positionScale );
        m_bakedImportScale = bakedScale;
        m_hasBakedImportScale = bakedScale > 0.f && std::isfinite( bakedScale );
    }
}

void Model::RecursiveLoadMesh( Moonlight::Node& root, EntityHandle& parentEnt, float InPositionScale )
{
    Transform& parentTransform = parentEnt->GetComponent<Transform>();
    World& world = *parentEnt.GetWorld();

    for( auto& childNode : root.Nodes )
    {
        // A saved scene already contains this node's entity (with any edits made to it): reuse it.
        EntityHandle entityNode;
        if( Transform* existing = parentTransform.GetChildByName( childNode.Name ) )
        {
            entityNode = existing->Parent;
            existing->SetPosition( existing->GetPosition() * InPositionScale );
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
        RecursiveLoadMesh( childNode, entityNode, InPositionScale );
    }

    if( root.Meshes.size() == 1 )
    {
        Mesh& meshRef = parentEnt->AddComponent<Mesh>( root.Meshes[0] );
        // AddComponent returns the deserialized Mesh when there is one; point it at the geometry.
        meshRef.BindModelMesh( root.Meshes[0] );
        if( root.IsFlipped && meshRef.MeshMaterial->FaceMode != Moonlight::RenderFaceMode::Both )
        {
            meshRef.MeshMaterial->FaceMode = Moonlight::RenderFaceMode::Back;
        }
    }
    else
    {
        for( size_t meshIndex = 0; meshIndex < root.Meshes.size(); ++meshIndex )
        {
            auto* child = root.Meshes[meshIndex];
            const std::string name = root.GetMeshEntityName( meshIndex );
            EntityHandle ent;
            Transform* existing = parentTransform.GetChildByName( name );
            const bool lastWithName = std::none_of( root.Meshes.begin() + meshIndex + 1, root.Meshes.end(), [child]( const Moonlight::MeshData* mesh ) { return mesh->Name == child->Name; } );
            const bool nodeWithName = std::any_of( root.Nodes.begin(), root.Nodes.end(), [child]( const Moonlight::Node& node ) { return node.Name == child->Name; } );
            if( !existing && lastWithName && !nodeWithName && name != child->Name )
            {
                // The old loader overwrote the same entity for every unnamed/material split.
                // Its final mesh/material belonged to the last split; retain that entity's edits.
                existing = parentTransform.GetChildByName( child->Name );
                if( existing && existing->Parent->HasComponent<Mesh>() ) existing->SetName( name );
                else existing = nullptr;
            }
            if( existing )
            {
                ent = existing->Parent;
                existing->SetPosition( existing->GetPosition() * InPositionScale );
            }
            else
            {
                ent = world.CreateEntity( name );
                Transform& transform = ent->AddComponent<Transform>();
                transform.SetParent( parentTransform );
            }
            Mesh& meshRef = ent->AddComponent<Mesh>( child );
            meshRef.BindModelMesh( child );
            if( root.IsFlipped && meshRef.MeshMaterial->FaceMode != Moonlight::RenderFaceMode::Both ) meshRef.MeshMaterial->FaceMode = Moonlight::RenderFaceMode::Back;
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
