#include "PCH.h"
#include "Ragdoll.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/PhysicsJoint.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include "World/SceneSerializer.h"
#include <algorithm>
#include <cmath>
#if USING( ME_EDITOR )
#include <imgui.h>
#endif

ME_REFLECT_BEGIN( Ragdoll )
    ME_FIELD( Simulating ).Tooltip( "Physics drives the bones; clearing it blends back to the animation" );
    ME_FIELD( BlendSeconds ).Range( 0.f, 5.f ).Tooltip( "Seconds to blend from the ragdoll back to the animation" );
    ME_FIELD( Bones ).CustomInspector();
    ME_FIELD( RadiusRatio ).Range( 0.01f, 1.f ).Category( "Build" ).Tooltip( "Capsule radius as a fraction of the bone's length" );
    ME_FIELD( SwingLimit ).Range( 0.f, 180.f ).Category( "Build" ).Tooltip( "The joints' cone half-angle, degrees" );
    ME_FIELD( TotalMass ).Range( 0.f, 10000.f ).Category( "Build" ).Tooltip( "Kilograms, shared out by bone length" );
    ME_FIELD( MinBoneLength ).Range( 0.f, 10.f ).Category( "Build" ).Tooltip( "Shorter bones get no body" );
ME_REFLECT_END()

namespace
{
    struct BoneInfo
    {
        Transform* Bone = nullptr;
        Transform* ParentBone = nullptr;
        Vector3 Offset;     // to the first child, local to the bone
        float Length = 0.f;
    };

    // Bones are descendants with a child far enough away, parents first, plus the leaf bones at
    // the ends of chains (a bone's childless child with no mesh), sized like their parent. A
    // bone's parent bone is its nearest ancestor that is one.
    void FindBones( Transform& InNode, const BoneInfo* InParentBone, float InMinLength, std::vector<BoneInfo>& OutBones )
    {
        for( Transform* child : InNode.GetChildren() )
        {
            const BoneInfo* parentBone = InParentBone;
            const bool mesh = child->Parent && ( child->Parent->GetComponentByName( "Mesh" ) || child->Parent->GetComponentByName( "Model" ) );
            BoneInfo bone{ child, InParentBone ? InParentBone->Bone : nullptr };
            if( !child->GetChildren().empty() )
            {
                bone.Offset = child->GetChildren().front()->GetPosition();
                bone.Length = bone.Offset.Length();
            }
            else if( InParentBone && !mesh )
            {
                bone.Offset = InParentBone->Offset;
                bone.Length = InParentBone->Length;
            }
            if( bone.Length >= InMinLength )
            {
                OutBones.push_back( bone );
                parentBone = &OutBones.back();
            }
            // The vector may grow below: hand the children a stable copy.
            if( parentBone && parentBone != InParentBone )
            {
                const BoneInfo copy = *parentBone;
                FindBones( *child, &copy, InMinLength, OutBones );
            }
            else FindBones( *child, parentBone, InMinLength, OutBones );
        }
    }

    int DominantAxis( const Vector3& InVector )
    {
        const float x = std::abs( InVector.x ), y = std::abs( InVector.y ), z = std::abs( InVector.z );
        return x >= y && x >= z ? 0 : y >= z ? 1 : 2;
    }

    const char* const kParts[] = { "PhysicsJoint", "CapsuleCollider", "Rigidbody" };
}


Ragdoll::Ragdoll()
    : Component( "Ragdoll" )
{
}


std::vector<Ragdoll::Part> Ragdoll::Plan() const
{
    std::vector<Part> parts;
    if( !Parent || !Parent->HasComponent<Transform>() ) return parts;
    std::vector<BoneInfo> bones;
    FindBones( Parent->GetComponent<Transform>(), nullptr, std::max( MinBoneLength, 1e-4f ), bones );
    float totalLength = 0.f;
    for( const BoneInfo& bone : bones ) totalLength += bone.Length;
    for( const BoneInfo& bone : bones )
    {
        if( !bone.Bone->Parent ) continue;
        const EntityHandle entity = bone.Bone->Parent;
        const int axis = DominantAxis( bone.Offset );
        const float radius = std::max( bone.Length * RadiusRatio, 0.005f );
        json body = { { "BodyType", Simulating ? "Dynamic" : "Kinematic" }, { "Mass", TotalMass * bone.Length / std::max( totalLength, 1e-4f ) },
            { "LinearDamping", 0.05 }, { "AngularDamping", 0.6 }, { "Interpolate", true } };
        parts.push_back( { entity, "Rigidbody", body } );
        // Along the bone, its caps reaching the joints at either end.
        const float along = axis == 0 ? bone.Offset.x : axis == 1 ? bone.Offset.y : bone.Offset.z;
        Vector3 center;
        ( axis == 0 ? center.x : axis == 1 ? center.y : center.z ) = along * 0.5f;
        parts.push_back( { entity, "CapsuleCollider", { { "Radius", radius }, { "Height", std::max( std::abs( along ), radius * 2.f ) },
            { "Direction", axis == 0 ? "X" : axis == 1 ? "Y" : "Z" }, { "Center", { center.x, center.y, center.z } } } } );
        if( bone.ParentBone && bone.ParentBone->Parent )
        {
            const Vector3 direction = bone.Offset * ( 1.f / bone.Length );
            parts.push_back( { entity, "PhysicsJoint", { { "JointType", "BallSocket" }, { "ConnectedBody", SceneSerializer::GUIDToString( bone.ParentBone->Parent->GetGUID() ) },
                { "Anchor", { 0.f, 0.f, 0.f } }, { "Axis", { direction.x, direction.y, direction.z } }, { "UseLimits", true }, { "UpperLimit", SwingLimit } } } );
        }
    }
    return parts;
}


int Ragdoll::Build()
{
    if( !Parent ) return 0;
    Clear();
    SerializationWorldScope scope( Parent.GetWorld() );
    Bones.clear();
    for( const Part& part : Plan() )
    {
        if( std::find( Bones.begin(), Bones.end(), part.Bone ) == Bones.end() ) Bones.push_back( part.Bone );
        BaseComponent* component = part.Bone->GetComponentByName( part.Type );
        if( !component ) component = part.Bone->AddComponentByName( part.Type );
        if( !component ) continue;
        component->Deserialize( part.State );
        component->OnPropertyChanged( "" );
    }
    m_applied = false;
    return static_cast<int>( Bones.size() );
}


void Ragdoll::Clear()
{
    for( EntityHandle& bone : Bones )
    {
        if( !bone ) continue;
        for( const char* type : kParts )
            if( bone->GetComponentByName( type ) ) bone->RemoveComponent( std::string( type ) );
    }
    Bones.clear();
    m_from.clear();
    m_blendLeft = 0.f;
}


void Ragdoll::ApplyBodyTypes()
{
    // The pose the bones had before the first ragdolling: a recovering bone the animation doesn't
    // drive goes back to it.
    if( !m_applied )
    {
        m_rest.clear();
        for( EntityHandle& bone : Bones )
        {
            Transform* transform = bone ? bone->TryGetComponent<Transform>() : nullptr;
            m_rest.emplace_back( transform ? transform->GetPosition() : Vector3(), transform ? transform->GetRotation() : Quaternion() );
        }
    }
    // Leaving the ragdoll blends from where the body lies now.
    const bool recovering = m_applied && m_appliedSimulating && !Simulating;
    m_blendLeft = 0.f;
    m_from.clear();
    if( recovering && BlendSeconds > 0.f )
    {
        for( EntityHandle& bone : Bones )
        {
            Transform* transform = bone ? bone->TryGetComponent<Transform>() : nullptr;
            m_from.emplace_back( transform ? transform->GetPosition() : Vector3(), transform ? transform->GetRotation() : Quaternion() );
        }
        m_blendLeft = BlendSeconds;
    }
    for( EntityHandle& bone : Bones )
    {
        Rigidbody* body = bone ? bone->TryGetComponent<Rigidbody>() : nullptr;
        if( !body ) continue;
        const BodyType type = Simulating ? BodyType::Dynamic : BodyType::Kinematic;
        if( body->Type != type )
        {
            body->Type = type;
            body->OnPropertyChanged( "Type" );
        }
    }
    m_applied = true;
    m_appliedSimulating = Simulating;
}


void Ragdoll::SetSimulating( bool InSimulating )
{
    Simulating = InSimulating;
    if( m_applied && m_appliedSimulating != Simulating ) ApplyBodyTypes();
}


void Ragdoll::ResetToRest()
{
    for( size_t i = 0; i < Bones.size() && i < m_rest.size(); ++i )
    {
        if( Transform* transform = Bones[i] ? Bones[i]->TryGetComponent<Transform>() : nullptr )
        {
            transform->SetPosition( m_rest[i].first );
            transform->SetRotation( m_rest[i].second );
        }
    }
    m_restApplied = true;
}


void Ragdoll::Blend( float InDeltaSeconds )
{
    if( !m_applied ) ApplyBodyTypes();
    if( m_blendLeft <= 0.f || Simulating )
    {
        m_restApplied = false;
        return;
    }
    // Without an Animator writing this frame, the target is the rest pose.
    if( !m_restApplied ) ResetToRest();
    m_restApplied = false;
    m_blendLeft = std::max( m_blendLeft - std::max( InDeltaSeconds, 0.f ), 0.f );
    // Smoothstep from the ragdoll's pose (weight 1) to the animation's (weight 0).
    const float t = BlendSeconds > 0.f ? m_blendLeft / BlendSeconds : 0.f;
    const float weight = t * t * ( 3.f - 2.f * t );
    for( size_t i = 0; i < Bones.size() && i < m_from.size(); ++i )
    {
        Transform* transform = Bones[i] ? Bones[i]->TryGetComponent<Transform>() : nullptr;
        if( !transform ) continue;
        const Vector3 animated = transform->GetPosition();
        transform->SetPosition( animated + ( m_from[i].first - animated ) * weight );
        transform->SetRotation( Quaternion::Slerp( transform->GetRotation(), m_from[i].second, weight ) );
    }
    if( m_blendLeft <= 0.f ) m_from.clear();
}


void Ragdoll::OnPropertyChanged( const std::string& InField )
{
    BlendSeconds = std::isfinite( BlendSeconds ) ? std::clamp( BlendSeconds, 0.f, 5.f ) : 0.4f;
    // Toggling Simulating applies at once (a ragdoll needn't have an Animator); other edits leave
    // a not-yet-applied ragdoll to its first animation update.
    if( ( InField == "Simulating" || m_applied ) && ( !m_applied || m_appliedSimulating != Simulating ) ) ApplyBodyTypes();
}


void Ragdoll::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_applied = false;
    m_blendLeft = 0.f;
    m_from.clear();
}


#if USING( ME_EDITOR )
void Ragdoll::OnEditorInspect()
{
    ImGui::TextDisabled( "%zu bones with ragdoll parts", Bones.size() );
    if( ImGui::Button( Bones.empty() ? "Build from skeleton" : "Rebuild from skeleton" ) )
    {
        RagdollBuildRequestEvent event;
        event.Owner = Parent;
        event.Fire();
    }
    ImGui::SetItemTooltip( "Adds a Rigidbody, a CapsuleCollider and a BallSocket joint to each bone below this entity (one undo step)" );
    if( !Bones.empty() )
    {
        ImGui::SameLine();
        if( ImGui::Button( "Remove parts" ) )
        {
            RagdollBuildRequestEvent event;
            event.Owner = Parent;
            event.Clear = true;
            event.Fire();
        }
    }
}
#endif
