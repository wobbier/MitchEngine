#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "ECS/EntityHandle.h"
#include "JSON.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <string>
#include <vector>

// GAME-5: a ragdoll over an expanded skeleton (bones are entities, as an imported Model's nodes
// are until the model is compacted). Each bone carries a Rigidbody, a CapsuleCollider along the
// bone and, below the first, a cone-limited BallSocket PhysicsJoint to its parent bone; Build()
// (Ragdoll > Build from skeleton in the inspector) makes them. While animated the bodies are
// kinematic and follow the pose (they push things and can be hit); Simulating makes them dynamic
// and the Animator stops writing the bones; clearing it blends from where the body fell back to
// the live animation over BlendSeconds.
class Ragdoll final : public Component<Ragdoll>
{
    ME_REFLECTABLE( Ragdoll )
public:
    Ragdoll();

    // The bones with ragdoll parts, parents first (Build fills it).
    std::vector<EntityHandle> Bones;
    bool Simulating = false;
    float BlendSeconds = 0.4f;
    // Build settings: capsule radius as a fraction of the bone's length, the joints' cone
    // half-angle, the mass shared out by bone length, and bones shorter than this are skipped.
    float RadiusRatio = 0.2f;
    float SwingLimit = 45.f;
    float TotalMass = 40.f;
    float MinBoneLength = 0.05f;

    struct Part
    {
        EntityHandle Bone;
        std::string Type;   // Rigidbody, CapsuleCollider, PhysicsJoint
        json State;
    };
    // What Build adds, without changing anything: the editor applies it as one undo step.
    std::vector<Part> Plan() const;
    // Adds the parts and fills Bones (code and tests; the editor uses Plan).
    int Build();
    // Removes the parts it added and clears Bones.
    void Clear();

    void SetSimulating( bool InSimulating );
    bool IsBlending() const { return m_blendLeft > 0.f; }
    // AnimationCore: before it writes a recovering ragdoll's pose, the bones go back to their rest
    // pose (so ones the animation doesn't drive recover too); after, Blend mixes in the fallen pose.
    void ResetToRest();
    void Blend( float InDeltaSeconds );

    void OnPropertyChanged( const std::string& InField ) override;
#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
    bool m_applied = false;
    bool m_appliedSimulating = false;
    float m_blendLeft = 0.f;
    std::vector<std::pair<Vector3, Quaternion>> m_from;
    std::vector<std::pair<Vector3, Quaternion>> m_rest;
    bool m_restApplied = false;
    void ApplyBodyTypes();
    void OnDeserialize( const json& InJson ) override;
};

ME_REGISTER_COMPONENT_FOLDER( Ragdoll, "Physics" )

// Asks the editor to build (or clear) a ragdoll as one undoable step.
#include "Events/Event.h"
class RagdollBuildRequestEvent
    : public Event<RagdollBuildRequestEvent>
{
public:
    RagdollBuildRequestEvent()
        : Event()
    {
    }

    EntityHandle Owner;
    bool Clear = false;
};
