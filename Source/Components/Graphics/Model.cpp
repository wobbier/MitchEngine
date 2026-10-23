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
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <functional>
#include <unordered_set>
#include "World/SceneSerializer.h"
#include "Components/Animation/Animator.h"
#include "Components/Graphics/TerrainFoliage.h"
#include "Cores/Rendering/RenderCore.h"
#include "Procedural/Tree/TreeTextures.h"

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
    // These transforms form a private tree, outside the World's component pools.
    for( auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it ) it->Pose->OnDestroy();
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


void Model::Reload()
{
    if( m_compact && IsInitialized )
    {
        for( const auto& part : m_parts ) part.Renderer->Serialize( m_savedParts[part.Path] );
        if( RenderCore* renderer = GetEngine().ModelRenderer ) renderer->ReleaseCompactParts( *Parent.Get() );
        m_parts.clear();
        for( auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it ) it->Pose->OnDestroy();
        m_nodes.clear();
        m_nodeLookup.clear();
        IsInitialized = false;
    }
    else if( IsInitialized )
    {
        return;   // expanded hierarchies own entities; they pick up changes when the scene reopens
    }
    ModelHandle = ResourceCache::GetInstance().GetAsync<ModelResource>( ModelPath );
    if( ModelHandle && ModelHandle->IsLoading() )
    {
        PendingModels().push_back( Parent );
        return;
    }
    Expand();
}


void Model::ReloadAll( World& InWorld, const std::string& InFullPath )
{
    const Path path( InFullPath );
    const std::string fullPath = path.FullPath;
    ResourceCache::GetInstance().Evict( fullPath );
    // Generated tree textures are sub-assets of the recipe.
    for( const std::string& channel : MitchTree::TextureChannels() )
    {
        ResourceCache::GetInstance().Evict( Path( MitchTree::ChannelPath( path.GetLocalPathString(), channel ) ).FullPath );
    }
    RenderCore* renderer = GetEngine().ModelRenderer;
    InWorld.ForEachEntity( [&]( Entity& entity ) {
        if( Model* model = entity.TryGetComponent<Model>(); model && model->ModelPath.FullPath == fullPath ) model->Reload();
        // Foliage re-fetches its species models when its draw is rebuilt.
        if( renderer && entity.HasComponent<TerrainFoliage>() ) renderer->RefreshFoliage( entity.GetId().Value() );
    } );
}


void Model::Expand()
{
    if( ModelHandle && !IsInitialized && !ModelHandle->HasLoadFailed() )
    {
        IsInitialized = true;
        if( m_compact )
        {
            BuildCompact();
            m_bakedImportScale = ModelHandle->GetBakedImportScale();
            m_hasBakedImportScale = m_bakedImportScale > 0.f;
            return;
        }
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

namespace
{
    // Names are escaped so a slash, percent or duplicate-name suffix cannot alias another path.
    std::string NodePathSegment( const std::string& InName, size_t InOccurrence )
    {
        std::string result;
        for( unsigned char c : InName )
        {
            if( c == '/' || c == '%' || c == '#' )
            {
                const char* hex = "0123456789ABCDEF";
                result += '%'; result += hex[c >> 4]; result += hex[c & 15];
            }
            else result += static_cast<char>( c );
        }
        return result + '#' + std::to_string( InOccurrence );
    }

    Matrix4 OffsetMatrix( const ModelNode& InNode )
    {
        return Matrix4( glm::translate( glm::mat4( 1.f ), InNode.PositionOffset.InternalVector )
            * glm::mat4_cast( glm::quat( InNode.RotationOffset.w, InNode.RotationOffset.x, InNode.RotationOffset.y, InNode.RotationOffset.z ) )
            * glm::scale( glm::mat4( 1.f ), InNode.ScaleOffset.InternalVector ) );
    }

    bool SameMatrix( const Matrix4& A, const Matrix4& B )
    {
        for( int c = 0; c < 4; ++c ) for( int r = 0; r < 4; ++r )
            if( std::abs( A.GetInternalMatrix()[c][r] - B.GetInternalMatrix()[c][r] ) > 1e-5f ) return false;
        return true;
    }
}

void Model::BuildCompact()
{
    AddCompactNode( ModelHandle->RootNode, -1, NodePathSegment( ModelHandle->RootNode.Name, 0 ) );
    UpdatePoseMatrices();
}

void Model::AddCompactNode( const Moonlight::Node& InNode, int InParent, const std::string& InPath )
{
    const int index = static_cast<int>( m_nodes.size() );
    InstanceNode node;
    node.Path = InPath;
    node.Source = &InNode;
    node.ParentIndex = InParent;
    node.Pose = std::make_unique<Transform>( InNode.Name );
    node.Pose->SetPosition( InNode.Position );
    node.Pose->SetRotation( InNode.Rotation );
    node.Pose->SetScale( InNode.Scale );
    const float positionScale = m_hasBakedImportScale && m_bakedImportScale > 0.f && ModelHandle->GetBakedImportScale() > 0.f
        ? ModelHandle->GetBakedImportScale() / m_bakedImportScale : 1.f;
    if( m_savedNodes.contains( InPath ) )
    {
        node.Pose->Deserialize( m_savedNodes[InPath] );
        node.Pose->SetPosition( node.Pose->GetPosition() * positionScale );
        node.Pose->Serialize( m_savedNodes[InPath] );
    }
    if( InParent >= 0 ) node.Pose->SetParent( *m_nodes[InParent].Pose );
    m_nodes.push_back( std::move( node ) );
    m_nodeLookup.emplace( InPath, index );
    m_nodeLookup.emplace( InNode.Name, index );
    std::unordered_map<std::string, size_t> meshOccurrences;
    for( auto* mesh : InNode.Meshes )
    {
        RenderPart part;
        part.NodeIndex = index;
        part.Path = InPath + "/@" + NodePathSegment( mesh->Name, meshOccurrences[mesh->Name]++ );
        part.Renderer = std::make_unique<Mesh>( mesh );
        part.Renderer->Parent = Parent;
        part.Renderer->Id = std::numeric_limits<unsigned int>::max();
        if( m_savedParts.contains( part.Path ) && m_savedParts[part.Path].contains( "Transform" ) )
        {
            Transform local;
            local.Deserialize( m_savedParts[part.Path]["Transform"] );
            local.SetPosition( local.GetPosition() * positionScale );
            local.Serialize( m_savedParts[part.Path]["Transform"] );
            part.Local = local.GetLocalMatrix();
        }
        if( m_savedParts.contains( part.Path ) )
        {
            part.Renderer->Deserialize( m_savedParts[part.Path] );
        }
        part.Renderer->BindModelMesh( mesh );
        if( InNode.IsFlipped && part.Renderer->MeshMaterial->FaceMode != Moonlight::RenderFaceMode::Both )
            part.Renderer->MeshMaterial->FaceMode = Moonlight::RenderFaceMode::Back;
        m_parts.push_back( std::move( part ) );
    }
    std::unordered_map<std::string, size_t> occurrences;
    for( const auto& child : InNode.Nodes )
        AddCompactNode( child, index, InPath + '/' + NodePathSegment( child.Name, occurrences[child.Name]++ ) );
}

int Model::FindNode( const std::string& InPathOrName ) const
{
    auto found = m_nodeLookup.find( InPathOrName );
    return found != m_nodeLookup.end() ? found->second : -1;
}

Transform* Model::GetNodeTransform( int InIndex ) const
{
    return InIndex >= 0 && static_cast<size_t>( InIndex ) < m_nodes.size() ? m_nodes[InIndex].Pose.get() : nullptr;
}

Matrix4 Model::GetNodeWorld( int InIndex ) const
{
    const Matrix4& owner = Parent->GetComponent<Transform>().GetLocalToWorldMatrix();
    Transform* node = GetNodeTransform( InIndex );
    return node ? Matrix4( owner.GetInternalMatrix() * node->GetLocalToWorldMatrix().GetInternalMatrix() ) : owner;
}

void Model::UpdatePoseMatrices()
{
    for( const auto& node : m_nodes ) node.Pose->GetLocalToWorldMatrix();
}

EntityHandle Model::GetNodeEntity( int InIndex ) const
{
    for( int i = InIndex; i >= 0; i = m_nodes[i].ParentIndex )
    {
        auto exposed = m_exposed.find( i );
        if( exposed != m_exposed.end() && exposed->second ) return exposed->second;
    }
    return Parent;
}

EntityHandle Model::ExposeNode( const std::string& InPath )
{
    const int index = FindNode( InPath );
    if( !m_compact || index < 0 ) return {};
    // Resolve saved/duplicated attachments before deciding whether another entity is needed.
    UpdateExposedNodes( *Parent.GetWorld() );
    auto existing = m_exposed.find( index );
    if( existing != m_exposed.end() && existing->second ) return existing->second;
    EntityHandle entity = Parent.GetWorld()->CreateEntity( m_nodes[index].Source->Name );
    Transform& transform = entity->AddComponent<Transform>();
    transform.SetParent( Parent->GetComponent<Transform>() );
    ModelNode& attachment = entity->AddComponent<ModelNode>();
    attachment.NodePath = m_nodes[index].Path;
    transform.SetWorldMatrix( GetNodeWorld( index ) );
    attachment.m_lastLocal = transform.GetLocalMatrix();
    attachment.m_written = true;
    m_exposed[index] = entity;
    return entity;
}

Model* ModelNode::GetModel() const
{
    Transform* transform = Parent ? Parent->TryGetComponent<Transform>() : nullptr;
    for( Transform* node = transform ? transform->GetParentTransform() : nullptr; node; node = node->GetParentTransform() )
        if( node->Parent ) if( Model* model = node->Parent->TryGetComponent<Model>() ) return model;
    return nullptr;
}

void Model::CaptureExposedEdits( World& InWorld )
{
    InWorld.ForEachEntity( []( Entity& entity ) {
        if( ModelNode* attachment = entity.TryGetComponent<ModelNode>() ) attachment->CaptureEdit();
    } );
}

void ModelNode::CaptureEdit()
{
    Transform* transform = Parent ? Parent->TryGetComponent<Transform>() : nullptr;
    Model* model = GetModel();
    const int index = model ? model->FindNode( NodePath ) : -1;
    if( !transform || !model || !model->IsCompact() || index < 0 || !m_written
        || SameMatrix( transform->GetLocalMatrix(), m_lastLocal ) ) return;
    const Matrix4 source = model->GetNodeWorld( index );
    if( std::abs( glm::determinant( source.GetInternalMatrix() ) ) < 1e-12f ) return;
    Transform offset;
    offset.SetWorldMatrix( source.Inverse() * transform->GetLocalToWorldMatrix() );
    PositionOffset = offset.GetPosition(); RotationOffset = offset.GetRotation(); ScaleOffset = offset.GetScale();
}

void Model::UpdateExposedNodes( World& InWorld )
{
    InWorld.ForEachEntity( []( Entity& entity ) {
        if( Model* model = entity.TryGetComponent<Model>() ) { model->m_exposed.clear(); model->m_externalParts.clear(); model->UpdatePoseMatrices(); }
    } );
    InWorld.ForEachEntity( []( Entity& entity ) {
        Transform* entityTransform = entity.TryGetComponent<Transform>();
        ModelNode* nodeAttachment = entity.TryGetComponent<ModelNode>();
        if( !entityTransform || !nodeAttachment ) return;
        Transform& transform = *entityTransform;
        ModelNode& attachment = *nodeAttachment;
        Model* model = attachment.GetModel();
        const int index = model ? model->FindNode( attachment.NodePath ) : -1;
        if( !model || !model->IsCompact() || index < 0 ) return;
        model->m_exposed.emplace( index, entity.GetHandle() );
        if( !attachment.PartPath.empty() && entity.HasComponent<Mesh>() ) model->m_externalParts.emplace( attachment.PartPath, entity.GetHandle() );
        const Matrix4 world( model->GetNodeWorld( index ).GetInternalMatrix() * OffsetMatrix( attachment ).GetInternalMatrix() );
        if( !SameMatrix( transform.GetLocalToWorldMatrix(), world ) ) transform.SetWorldMatrix( world );
        attachment.m_lastLocal = transform.GetLocalMatrix();
        attachment.m_written = true;
    } );
}

bool Model::IsPartExternal( const RenderPart& InPart ) const
{
    auto external = m_externalParts.find( InPart.Path );
    if( external != m_externalParts.end() && external->second && external->second->HasComponent<Mesh>() ) return true;
    return m_savedParts.contains( InPart.Path ) && m_savedParts[InPart.Path].value( "UseOwnerMesh", false ) && Parent->HasComponent<Mesh>();
}

AABB Model::GetWorldBounds( int InNode ) const
{
    AABB bounds;
    for( const auto& part : m_parts )
    {
        bool included = InNode < 0;
        for( int n = part.NodeIndex; n >= 0 && !included; n = m_nodes[n].ParentIndex ) included = n == InNode;
        if( included )
        {
            const AABB partBounds = part.Renderer->GetWorldBounds( GetPartWorld( part ) );
            if( partBounds.IsValid() ) bounds.Encapsulate( partBounds );
        }
    }
    return bounds;
}

void Model::OnSerialize( json& Out )
{
    Out["ModelPath"] = ModelPath.GetLocalPath();
    if( m_hasBakedImportScale ) Out["BakedImportScale"] = m_bakedImportScale;
    Out["Compact"] = m_compact;
    if( m_compact )
    {
        Out["Nodes"] = m_savedNodes;
        Out["Parts"] = m_savedParts;
        for( const auto& part : m_parts ) part.Renderer->Serialize( Out["Parts"][part.Path] );
    }
}

void Model::OnDeserialize( const json& In )
{
    ModelPath = Path( In["ModelPath"] );
    m_hasBakedImportScale = In.contains( "BakedImportScale" );
    m_bakedImportScale = In.value( "BakedImportScale", 1.f );
    m_compact = In.value( "Compact", false );
    m_savedParts = In.value( "Parts", json::object() );
    m_savedNodes = In.value( "Nodes", json::object() );
    // Component undo also restores material overrides on an already-loaded instance.
    for( auto& part : m_parts ) if( m_savedParts.contains( part.Path ) )
    {
        part.Renderer->Deserialize( m_savedParts[part.Path] );
        part.Renderer->BindModelMesh( part.Renderer->MeshReferece );
    }
}

bool Model::CompactHierarchy()
{
    if( m_compact || !IsInitialized || !ModelHandle ) return false;
    BuildCompact();
    std::unordered_map<const Moonlight::Node*, int> indices;
    for( size_t i = 0; i < m_nodes.size(); ++i ) indices[m_nodes[i].Source] = static_cast<int>( i );
    std::unordered_map<uint64_t, std::pair<EntityHandle, int>> generated;
    std::unordered_map<uint64_t, std::string> meshPaths;
    std::function<void( const Moonlight::Node&, EntityHandle )> collect = [&]( const Moonlight::Node& source, EntityHandle entity ) {
        if( !entity ) return;
        const int index = indices.at( &source );
        Transform& transform = entity->GetComponent<Transform>();
        if( entity != Parent )
        {
            generated[entity->GetGUID()] = { entity, index };
            json saved;
            transform.Serialize( saved );
            m_savedNodes[m_nodes[index].Path] = saved;
            m_nodes[index].Pose->Deserialize( saved );
        }
        size_t meshIndex = 0;
        for( auto& part : m_parts )
        {
            if( part.NodeIndex != index ) continue;
            Transform* meshTransform = source.Meshes.size() == 1 ? &transform : transform.GetChildByName( source.GetMeshEntityName( meshIndex ) );
            ++meshIndex;
            if( !meshTransform ) continue;
            EntityHandle meshEntity = meshTransform->Parent;
            if( Mesh* old = meshEntity->TryGetComponent<Mesh>() )
            {
                json saved;
                old->Serialize( saved );
                if( meshEntity == Parent ) saved["UseOwnerMesh"] = true;
                if( source.Meshes.size() != 1 )
                {
                    meshTransform->Serialize( saved["Transform"] );
                    part.Local = meshTransform->GetLocalMatrix();
                    generated[meshEntity->GetGUID()] = { meshEntity, index };
                }
                m_savedParts[part.Path] = saved;
                part.Renderer->Deserialize( saved );
                part.Renderer->BindModelMesh( old->MeshReferece );
                meshPaths[meshEntity->GetGUID()] = part.Path;
            }
        }
        for( const auto& child : source.Nodes )
            if( Transform* found = transform.GetChildByName( child.Name ) ) collect( child, found->Parent );
    };
    const Moonlight::Node* root = &ModelHandle->RootNode;
    if( root->Meshes.empty() && root->Nodes.size() == 1 && IsIdentity( *root ) && IsIdentity( root->Nodes[0] ) && ModelHandle->GetAnimations().empty() ) root = &root->Nodes[0];
    Transform& owner = Parent->GetComponent<Transform>();
    Transform* importedRoot = owner.GetChildByName( root->Name );
    collect( *root, importedRoot ? importedRoot->Parent : Parent );

    // Keep authored components, user-created descendants and every entity referenced by another
    // component. Only generated import structure is eligible for removal.
    std::unordered_set<uint64_t> keep;
    for( const auto& [guid, entry] : generated )
    {
        for( BaseComponent* component : entry.first->GetAllComponents() )
            if( component->GetTypeId() != Transform::GetStaticTypeId() && component->GetTypeId() != Mesh::GetStaticTypeId() ) keep.insert( guid );
        for( Transform* child : entry.first->GetComponent<Transform>().GetChildren() )
            if( child->Parent && !generated.count( child->Parent->GetGUID() ) ) keep.insert( guid );
    }
    std::function<void( const json& )> references = [&]( const json& value ) {
        if( value.is_string() || value.is_number_unsigned() )
        {
            const uint64_t guid = SceneSerializer::GUIDFromJson( value );
            if( generated.count( guid ) ) keep.insert( guid );
        }
        else if( value.is_structured() ) for( const auto& child : value ) references( child );
    };
    Parent.GetWorld()->ForEachEntity( [&]( Entity& entity ) {
        for( BaseComponent* component : entity.GetAllComponents() ) { json saved; component->Serialize( saved ); references( saved ); }
    } );
    m_compact = true;
    if( Animator* animator = Parent->TryGetComponent<Animator>() ) animator->InvalidateBindings();
    UpdatePoseMatrices();
    for( const auto& [guid, entry] : generated )
    {
        if( !keep.count( guid ) ) continue;
        Transform& transform = entry.first->GetComponent<Transform>();
        const Matrix4 world = transform.GetLocalToWorldMatrix();
        transform.SetParent( owner, true );
        ModelNode& exposed = entry.first->AddComponent<ModelNode>();
        exposed.NodePath = m_nodes[entry.second].Path;
        auto meshPath = meshPaths.find( guid );
        if( meshPath != meshPaths.end() ) exposed.PartPath = meshPath->second;
        Transform offset;
        offset.SetWorldMatrix( GetNodeWorld( entry.second ).Inverse() * world );
        exposed.PositionOffset = offset.GetPosition();
        exposed.RotationOffset = offset.GetRotation();
        exposed.ScaleOffset = offset.GetScale();
    }
    for( const auto& [guid, entry] : generated ) if( !keep.count( guid ) ) entry.first->MarkForDelete();
    UpdateExposedNodes( *Parent.GetWorld() );
    return true;
}

void ModelNode::OnSerialize( json& Out )
{
    CaptureEdit(); // a save can follow a gizmo edit before the next animation update
    Out["NodePath"] = NodePath;
    if( !PartPath.empty() ) Out["PartPath"] = PartPath;
    Out["PositionOffset"] = { PositionOffset.x, PositionOffset.y, PositionOffset.z };
    Out["RotationOffset"] = { RotationOffset.x, RotationOffset.y, RotationOffset.z, RotationOffset.w };
    Out["ScaleOffset"] = { ScaleOffset.x, ScaleOffset.y, ScaleOffset.z };
}

void ModelNode::OnDeserialize( const json& In )
{
    NodePath = In.value( "NodePath", std::string() );
    PartPath = In.value( "PartPath", std::string() );
    if( In.contains( "PositionOffset" ) ) PositionOffset = Vector3( In["PositionOffset"][0], In["PositionOffset"][1], In["PositionOffset"][2] );
    if( In.contains( "RotationOffset" ) ) RotationOffset = Quaternion( In["RotationOffset"][0], In["RotationOffset"][1], In["RotationOffset"][2], In["RotationOffset"][3] );
    if( In.contains( "ScaleOffset" ) ) ScaleOffset = Vector3( In["ScaleOffset"][0], In["ScaleOffset"][1], In["ScaleOffset"][2] );
    m_written = false;
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

void Model::OnEditorInspect()
{
    if( ModelHandle )
    {
        HavanaUtils::Label( "Loaded Path" );
        ImGui::TextUnformatted( ModelPath.GetLocalPath().data() );
        if( m_compact )
        {
            ImGui::Text( "%zu render parts, %zu exposed nodes", m_parts.size(), m_exposed.size() );
            if( ImGui::TreeNodeEx( "Model nodes", ImGuiTreeNodeFlags_FramePadding ) )
            {
                std::function<void( int )> draw = [&]( int index ) {
                    const auto& node = m_nodes[index];
                    ImGui::PushID( node.Path.c_str() );
                    const bool open = ImGui::TreeNodeEx( "node", ImGuiTreeNodeFlags_FramePadding, "%s", node.Source->Name.c_str() );
                    ImGui::SameLine();
                    if( m_exposed.count( index ) ) ImGui::TextDisabled( "Exposed" );
                    else if( ImGui::Button( "Expose as entity" ) )
                    {
                        ModelHierarchyEvent event; event.Target = Parent; event.NodePath = node.Path; event.Queue();
                    }
                    if( open )
                    {
                        for( auto& part : m_parts ) if( part.NodeIndex == index )
                        {
                            ImGui::PushID( part.Path.c_str() );
                            if( ImGui::TreeNodeEx( "Material", ImGuiTreeNodeFlags_FramePadding ) ) { part.Renderer->OnEditorInspect(); ImGui::TreePop(); }
                            ImGui::PopID();
                        }
                        for( size_t child = index + 1; child < m_nodes.size(); ++child )
                            if( m_nodes[child].ParentIndex == index ) draw( static_cast<int>( child ) );
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                };
                if( !m_nodes.empty() ) draw( 0 );
                ImGui::TreePop();
            }
        }
        else
        {
            ImGui::TextDisabled( "Expanded hierarchy" );
            if( ImGui::Button( "Compact hierarchy" ) )
            {
                ModelHierarchyEvent event; event.Target = Parent; event.Queue();
            }
        }
        if( !ModelHandle->GetAnimations().empty() && ImGui::TreeNodeEx( "Animations", ImGuiTreeNodeFlags_FramePadding ) )
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
        const float width = HavanaUtils::Label( "Model" );
        if( ImGui::Button( ( ( !ModelPath.GetLocalPath().empty() ) ? ModelPath.GetLocalPath().data() : "Select Asset" ), ImVec2( width, 0.f ) ) )
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

void ModelNode::OnEditorInspect()
{
    ImGui::TextWrapped( "Source node: %s", NodePath.c_str() );
    ImGui::TextWrapped( "Follows this model node. Move this entity to adjust its attachment offset; parent cameras and other objects beneath it." );
    if( ImGui::Button( "Reset attachment offset" ) )
    {
        PositionOffset = Vector3(); RotationOffset = Quaternion(); ScaleOffset = Vector3( 1.f );
    }
}

#endif
