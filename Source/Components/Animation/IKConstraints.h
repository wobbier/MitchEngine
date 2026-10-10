#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "ECS/EntityHandle.h"
#include "Math/Vector3.h"

// Inverse kinematics, applied by AnimationCore after the animators write their poses (and before the
// late update), while the game runs. Bones are the node entities a Model creates (they keep their
// GUIDs in the scene), so constraints can sit on any entity, usually the character.

// Bends a limb so its end reaches a target: Tip is the end bone (foot, hand), its parent the middle
// joint (knee, elbow) and its grandparent the root (thigh, upper arm).
class TwoBoneIK
    : public Component<TwoBoneIK>
{
    ME_REFLECTABLE( TwoBoneIK )
public:
    TwoBoneIK();

    EntityHandle Tip;
    EntityHandle Target;
    // Optional: the middle joint bends toward it (a knee forward, an elbow out); without one the limb
    // keeps the bend direction the animation gave it.
    EntityHandle Pole;
    // 0 leaves the animated pose, 1 reaches the target.
    float Weight = 1.f;
    // Also turn the tip to the target's rotation (a hand gripping a handle, a foot flat on a slope).
    bool MatchTargetRotation = false;

    // After the last solve: whether the target was within reach.
    bool IsReached() const
    {
        return m_reached;
    }

private:
    friend class AnimationCore;
    bool m_reached = true;
};
ME_REGISTER_COMPONENT_FOLDER( TwoBoneIK, "Animation" )

// Turns a bone so its AimAxis points at a target (a head looking at the player, a turret), spreading
// the turn over ChainLength bones up the hierarchy (spine, neck, head) so no single joint twists.
class LookAtIK
    : public Component<LookAtIK>
{
    ME_REFLECTABLE( LookAtIK )
public:
    LookAtIK();

    // The bone that aims; empty aims this entity.
    EntityHandle Bone;
    EntityHandle Target;
    // The bone's forward direction, in its own space.
    Vector3 AimAxis = Vector3( 0.f, 0.f, 1.f );
    float Weight = 1.f;
    // Furthest the aim turns from the animated pose, in degrees (beyond it, it stops at the limit).
    float MaxAngle = 90.f;
    // Bones sharing the turn: the aiming bone and its parents.
    int ChainLength = 1;
};
ME_REGISTER_COMPONENT_FOLDER( LookAtIK, "Animation" )
