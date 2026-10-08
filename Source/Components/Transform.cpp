#include "PCH.h"
#include "Transform.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <atomic>
#include "Jobs/JobSystem.h"


namespace
{
    // Set whenever any transform becomes dirty; lets UpdateAll skip fully static frames.
    std::atomic<bool> s_anyTransformDirty{ true };
}

ME_REFLECT_BEGIN( Transform )
    ME_FIELD_NAMED( LocalPosition, "Position" ).Speed( 0.1f );
    ME_FIELD_NAMED( LocalRotation, "Rotation" );
    ME_FIELD_NAMED( LocalScale, "Scale" ).Speed( 0.05f );
ME_REFLECT_END()


Transform::Transform()
    : Component( "Transform" )
    , LocalPosition( 0.f, 0.f, 0.f )
    , LocalScale( 1.0f, 1.0f, 1.0f )
{
    s_anyTransformDirty.store( true, std::memory_order_relaxed );
}


Transform::Transform( const std::string& TransformName )
    : Component( "Transform" )
    , LocalPosition( 0.f, 0.f, 0.f )
    , LocalScale( 1.0f, 1.0f, 1.0f )
    , m_pendingName( TransformName )
{
    s_anyTransformDirty.store( true, std::memory_order_relaxed );
}


Transform::~Transform()
{
    // Hierarchy links are torn down in OnDestroy, which the World always calls (children first)
    // before destroying a transform. Touching other transforms here would be unsafe during
    // whole-pool teardown.
}


void Transform::Init()
{
    if( !m_pendingName.empty() )
    {
        SetName( m_pendingName );
        m_pendingName.clear();
    }
}


void Transform::OnDestroy()
{
    if( m_parent )
    {
        auto& siblings = m_parent->m_children;
        siblings.erase( std::remove( siblings.begin(), siblings.end(), this ), siblings.end() );
        m_parent = nullptr;
    }

    World* world = Parent.GetWorld();
    for( Transform* child : m_children )
    {
        child->m_parent = nullptr;
        child->MarkDirty();
        if( world )
        {
            world->MarkEntityDirty( child->Parent.GetID() );
        }
    }
    m_children.clear();
}


void Transform::OnPropertyChanged( const std::string& InFieldName )
{
    MarkDirty();
}

// ------------------------------------------------------------------------------------------------
// Local space
// ------------------------------------------------------------------------------------------------

const Vector3& Transform::GetPosition() const
{
    return LocalPosition;
}


void Transform::SetPosition( const Vector3& NewPosition )
{
    LocalPosition = NewPosition;
    MarkDirty();
}


const Quaternion& Transform::GetRotation() const
{
    return LocalRotation;
}


void Transform::SetRotation( const Quaternion& InRotation )
{
    LocalRotation = InRotation;
    MarkDirty();
}


void Transform::SetRotation( const Vector3& euler )
{
    LocalRotation = Quaternion::FromEulerDegrees( euler );
    MarkDirty();
}


Vector3 Transform::GetRotationEuler() const
{
    return Quaternion::ToEulerAngles( LocalRotation );
}


const Vector3& Transform::GetScale() const
{
    return LocalScale;
}


void Transform::SetScale( const Vector3& NewScale )
{
    if( NewScale.IsZero() )
    {
        return;
    }
    LocalScale = NewScale;
    MarkDirty();
}


void Transform::SetScale( float NewScale )
{
    SetScale( Vector3( NewScale ) );
}

// ------------------------------------------------------------------------------------------------
// World space
// ------------------------------------------------------------------------------------------------

Vector3 Transform::GetWorldPosition()
{
    return GetLocalToWorldMatrix().GetPosition();
}


void Transform::SetWorldPosition( const Vector3& NewPosition )
{
    SetPosition( m_parent ? m_parent->GetWorldToLocalMatrix().TransformPoint( NewPosition ) : NewPosition );
}


Quaternion Transform::GetWorldRotation()
{
    return m_parent ? m_parent->GetWorldRotation() * LocalRotation : LocalRotation;
}


void Transform::SetWorldRotation( const Quaternion& inRotation )
{
    SetRotation( m_parent ? m_parent->GetWorldRotation().Inverse() * inRotation : inRotation );
}


Vector3 Transform::GetWorldRotationEuler()
{
    return Quaternion::ToEulerAngles( GetWorldRotation() );
}


Vector3 Transform::GetWorldScale()
{
    const glm::mat4& world = GetLocalToWorldMatrix().GetInternalMatrix();
    return Vector3( glm::length( glm::vec3( world[0] ) ), glm::length( glm::vec3( world[1] ) ), glm::length( glm::vec3( world[2] ) ) );
}


void Transform::SetWorldMatrix( const Matrix4& NewWorld )
{
    const glm::mat4 local = m_parent ? glm::inverse( m_parent->GetLocalToWorldMatrix().GetInternalMatrix() ) * NewWorld.GetInternalMatrix() : NewWorld.GetInternalMatrix();

    glm::vec3 scale;
    glm::quat rotation;
    glm::vec3 translation;
    glm::vec3 skew;
    glm::vec4 perspective;
    if( glm::decompose( local, scale, rotation, translation, skew, perspective ) )
    {
        LocalPosition = Vector3( translation );
        LocalRotation = Quaternion( glm::normalize( rotation ) );
        LocalScale = Vector3( scale );
        MarkDirty();
    }
}


Vector3 Transform::Front()
{
    return GetWorldRotation().Rotate( Vector3::Front );
}


Vector3 Transform::Up()
{
    return GetWorldRotation().Rotate( Vector3::Up );
}


Vector3 Transform::Right()
{
    return GetWorldRotation().Rotate( Vector3::Right );
}


void Transform::LookAt( const Vector3& InWorldTarget, const Vector3& InUp )
{
    LookDirection( InWorldTarget - GetWorldPosition(), InUp );
}


void Transform::LookDirection( const Vector3& InDirection, const Vector3& InUp )
{
    if( InDirection.IsZero() )
    {
        return;
    }
    SetWorldRotation( Quaternion::LookRotation( InDirection, InUp ) );
}


void Transform::Translate( const Vector3& InDelta, TransformSpace InSpace )
{
    if( InDelta.IsZero() )
    {
        return;
    }
    const Vector3 worldDelta = InSpace == TransformSpace::World ? InDelta : GetWorldRotation().Rotate( InDelta );
    SetWorldPosition( GetWorldPosition() + worldDelta );
}


void Transform::Rotate( const Vector3& inDegrees, TransformSpace inRelativeTo )
{
    const Quaternion delta = Quaternion::FromEulerDegrees( inDegrees );
    if( inRelativeTo == TransformSpace::World )
    {
        SetWorldRotation( delta * GetWorldRotation() );
    }
    else
    {
        SetRotation( LocalRotation * delta );
    }
}


Vector3 Transform::TransformPoint( const Vector3& InLocalPoint )
{
    return GetLocalToWorldMatrix().TransformPoint( InLocalPoint );
}


Vector3 Transform::TransformDirection( const Vector3& InLocalDirection )
{
    return GetWorldRotation().Rotate( InLocalDirection );
}


Vector3 Transform::InverseTransformPoint( const Vector3& InWorldPoint )
{
    return GetWorldToLocalMatrix().TransformPoint( InWorldPoint );
}


Vector3 Transform::InverseTransformDirection( const Vector3& InWorldDirection )
{
    return GetWorldRotation().Inverse().Rotate( InWorldDirection );
}


void Transform::Reset()
{
    LocalPosition = Vector3();
    LocalRotation = Quaternion::Identity;
    LocalScale = Vector3( 1.f );
    MarkDirty();
}

// ------------------------------------------------------------------------------------------------
// Hierarchy
// ------------------------------------------------------------------------------------------------

void Transform::SetParent( Transform& NewParent, bool keepWorldTransform )
{
    if( &NewParent == this || NewParent.IsDescendantOf( *this ) )
    {
        BRUH( "Transform::SetParent: refusing to create a cycle in the hierarchy." );
        return;
    }
    if( m_parent == &NewParent )
    {
        return;
    }

    const Matrix4 worldBefore = keepWorldTransform ? GetLocalToWorldMatrix() : Matrix4();
    if( m_parent )
    {
        auto& siblings = m_parent->m_children;
        siblings.erase( std::remove( siblings.begin(), siblings.end(), this ), siblings.end() );
    }

    m_parent = &NewParent;
    NewParent.m_children.push_back( this );

    if( keepWorldTransform )
    {
        SetWorldMatrix( worldBefore );
    }
    MarkDirty();

    if( World* world = Parent.GetWorld() )
    {
        world->MarkEntityDirty( Parent.GetID() );
    }
}


void Transform::DetachFromParent( bool keepWorldTransform )
{
    if( !m_parent )
    {
        return;
    }

    const Matrix4 worldBefore = keepWorldTransform ? GetLocalToWorldMatrix() : Matrix4();
    auto& siblings = m_parent->m_children;
    siblings.erase( std::remove( siblings.begin(), siblings.end(), this ), siblings.end() );
    m_parent = nullptr;

    if( keepWorldTransform )
    {
        SetWorldMatrix( worldBefore );
    }
    MarkDirty();

    if( World* world = Parent.GetWorld() )
    {
        world->MarkEntityDirty( Parent.GetID() );
    }
}


void Transform::RemoveChild( Transform* TargetTransform )
{
    if( TargetTransform && TargetTransform->m_parent == this )
    {
        TargetTransform->DetachFromParent( false );
    }
}


Transform* Transform::GetParentTransform() const
{
    return m_parent;
}


const std::vector<Transform*>& Transform::GetChildren() const
{
    return m_children;
}


Transform* Transform::GetChildByName( const std::string& inName ) const
{
    for( Transform* child : m_children )
    {
        if( child->GetName() == inName )
        {
            return child;
        }
    }
    return nullptr;
}


Transform* Transform::FindDescendantByName( const std::string& inName ) const
{
    for( Transform* child : m_children )
    {
        if( child->GetName() == inName )
        {
            return child;
        }
        if( Transform* found = child->FindDescendantByName( inName ) )
        {
            return found;
        }
    }
    return nullptr;
}


bool Transform::IsDescendantOf( const Transform& InAncestor ) const
{
    for( const Transform* node = m_parent; node; node = node->m_parent )
    {
        if( node == &InAncestor )
        {
            return true;
        }
    }
    return false;
}


size_t Transform::GetSiblingIndex() const
{
    if( !m_parent )
    {
        return 0;
    }
    const auto& siblings = m_parent->m_children;
    return static_cast<size_t>( std::find( siblings.begin(), siblings.end(), this ) - siblings.begin() );
}


void Transform::SetSiblingIndex( size_t InIndex )
{
    if( !m_parent )
    {
        return;
    }
    auto& siblings = m_parent->m_children;
    siblings.erase( std::remove( siblings.begin(), siblings.end(), this ), siblings.end() );
    InIndex = std::min( InIndex, siblings.size() );
    siblings.insert( siblings.begin() + static_cast<std::ptrdiff_t>( InIndex ), this );
}

// ------------------------------------------------------------------------------------------------
// Matrices
// ------------------------------------------------------------------------------------------------

void Transform::MarkDirty()
{
    m_isInverseDirty = true;
    if( m_isWorldDirty )
    {
        // Descendants of a dirty transform are always dirty already.
        return;
    }
    m_isWorldDirty = true;
    s_anyTransformDirty.store( true, std::memory_order_relaxed );
    for( Transform* child : m_children )
    {
        child->MarkDirty();
    }
}


bool Transform::IsDirty() const
{
    return m_isWorldDirty;
}


Matrix4 Transform::GetLocalMatrix() const
{
    const glm::mat4 T = glm::translate( glm::mat4( 1.0f ), LocalPosition.InternalVector );
    const glm::mat4 R = glm::toMat4( LocalRotation.InternalQuat );
    const glm::mat4 S = glm::scale( glm::mat4( 1.0f ), LocalScale.InternalVector );
    return Matrix4( T * R * S );
}


const Matrix4& Transform::GetLocalToWorldMatrix()
{
    if( m_isWorldDirty )
    {
        const glm::mat4 local = GetLocalMatrix().GetInternalMatrix();
        LocalToWorldMatrix = m_parent ? Matrix4( m_parent->GetLocalToWorldMatrix().GetInternalMatrix() * local ) : Matrix4( local );
        m_isWorldDirty = false;
    }
    return LocalToWorldMatrix;
}


const Matrix4& Transform::GetMatrix()
{
    return GetLocalToWorldMatrix();
}


const Matrix4& Transform::GetWorldToLocalMatrix()
{
    if( m_isInverseDirty || m_isWorldDirty )
    {
        WorldToLocalMatrix = GetLocalToWorldMatrix().Inverse();
        m_isInverseDirty = false;
    }
    return WorldToLocalMatrix;
}


namespace
{
    // Recomputes dirty world matrices below an already-clean transform. Each subtree is touched by
    // exactly one thread, and parents are always clean before their children are visited.
    void UpdateSubtree( Transform& transform, bool parentChanged )
    {
        bool changed = parentChanged || transform.IsDirty();
        if( changed )
        {
            transform.GetLocalToWorldMatrix();
        }
        for( Transform* child : transform.GetChildren() )
        {
            UpdateSubtree( *child, changed );
        }
    }
}


void Transform::UpdateAll( World& InWorld )
{
    OPTICK_EVENT( "Transform::UpdateAll" );
    if( !s_anyTransformDirty.exchange( false, std::memory_order_acq_rel ) )
    {
        return;
    }

    struct Pending
    {
        Transform* Node;
        bool ParentChanged;
    };

    // Walk down from the roots one level at a time (serially) until there are enough independent
    // subtrees to keep every thread busy, then finish each subtree on its own thread. Deep scenes
    // with a single root still parallelize this way.
    std::vector<Pending> frontier;
    InWorld.GetComponentPool<Transform>().ForEach( [&frontier]( uint32_t, Transform& transform ) {
        if( !transform.m_parent )
        {
            frontier.push_back( { &transform, false } );
        }
    } );

    Jobs::JobSystem& jobs = Jobs::JobSystem::Get();
    const size_t targetSubtrees = static_cast<size_t>( jobs.GetThreadCount() ) * 4;
    std::vector<Pending> next;
    while( !frontier.empty() && frontier.size() < targetSubtrees )
    {
        next.clear();
        for( const Pending& pending : frontier )
        {
            const bool changed = pending.ParentChanged || pending.Node->m_isWorldDirty;
            if( changed )
            {
                pending.Node->GetLocalToWorldMatrix();
            }
            for( Transform* child : pending.Node->m_children )
            {
                next.push_back( { child, changed } );
            }
        }
        frontier.swap( next );
    }

    jobs.ParallelFor( static_cast<uint32_t>( frontier.size() ), 1, [&frontier]( uint32_t begin, uint32_t end ) {
        for( uint32_t i = begin; i < end; ++i )
        {
            UpdateSubtree( *frontier[i].Node, frontier[i].ParentChanged );
        }
    } );
}

// ------------------------------------------------------------------------------------------------
// Naming / serialization
// ------------------------------------------------------------------------------------------------

const std::string& Transform::GetName() const
{
    if( Entity* entity = Parent.Get() )
    {
        return entity->GetName();
    }
    return m_pendingName;
}


void Transform::SetName( const std::string& name )
{
    if( Entity* entity = Parent.Get() )
    {
        entity->SetName( name );
    }
    else
    {
        m_pendingName = name;
    }
}


void Transform::OnSerialize( json& outJson )
{
    Reflection::ToJson( StaticType(), this, outJson );
}


void Transform::OnDeserialize( const json& inJson )
{
    // Scenes saved before scene format v2 store rotation as Euler degrees.
    auto rotation = inJson.find( "Rotation" );
    if( rotation != inJson.end() && rotation->is_array() && rotation->size() == 3 )
    {
        json withoutRotation = inJson;
        withoutRotation.erase( "Rotation" );
        Reflection::FromJson( StaticType(), this, withoutRotation );
        LocalRotation = Quaternion::FromEulerDegrees( Vector3( ( *rotation )[0].get<float>(), ( *rotation )[1].get<float>(), ( *rotation )[2].get<float>() ) );
    }
    else
    {
        Reflection::FromJson( StaticType(), this, inJson );
    }
    MarkDirty();
}

#if USING( ME_EDITOR )

void Transform::OnEditorInspect()
{
    if( Entity* entity = Parent.Get() )
    {
        std::string name = entity->GetName();
        HavanaUtils::Label( "Name" );
        if( ImGui::InputText( "##Name", &name ) )
        {
            entity->SetName( name );
        }
    }

    Vector3 OldPosition = LocalPosition;
    if( HavanaUtils::EditableVector3( "Local Position", OldPosition ) )
    {
        SetPosition( OldPosition );
    }

    Vector3 OldRotation = Quaternion::ToEulerAngles( GetRotation() );
    if( HavanaUtils::EditableVector3( "Local Rotation", OldRotation ) )
    {
        SetRotation( OldRotation );
    }

    Vector3 OldScale = LocalScale;
    if( HavanaUtils::EditableVector3( "Scale", OldScale, 1.f ) )
    {
        SetScale( OldScale );
    }

    Vector3 WorldPos = GetWorldPosition();
    if( HavanaUtils::EditableVector3( "World Position", WorldPos ) )
    {
        SetWorldPosition( WorldPos );
    }

    Vector3 OldWorldRotation = GetWorldRotationEuler();
    if( HavanaUtils::EditableVector3( "World Rotation", OldWorldRotation ) )
    {
        SetWorldRotation( Quaternion::FromEulerDegrees( OldWorldRotation ) );
    }

    if( ImGui::Button( "Reset Transform" ) )
    {
        Reset();
    }
}

#endif


TransformHandle::TransformHandle( Transform* InTransform )
    : m_entity( InTransform ? InTransform->Parent : EntityHandle() )
{
}


Transform* TransformHandle::Get() const
{
    Entity* entity = m_entity.Get();
    return entity ? entity->TryGetComponent<Transform>() : nullptr;
}
