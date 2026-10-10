#pragma once
#include "Math/Quaternion.h"
#include "Math/Vector3.h"

// Inverse kinematics math, in world space (the IK components and AnimationCore apply it to bones).
namespace IK
{
    // The shortest rotation turning direction InFrom into InTo (identity for zero vectors).
    Quaternion FromTo( const Vector3& InFrom, const Vector3& InTo );

    // World-space rotations that bend a two-bone chain (root -> mid -> tip) so the tip lands on the
    // target, or as close as the chain reaches. Pre-multiply RootDelta onto the root bone's world
    // rotation, then MidDelta onto the mid bone's.
    struct TwoBoneSolution
    {
        Quaternion RootDelta;
        Quaternion MidDelta;
        bool Reached = true;    // false when the target is out of reach (the chain points at it)
    };
    // The chain bends toward InPole when InUsePole, else keeps its current bend direction.
    TwoBoneSolution SolveTwoBone( const Vector3& InRoot, const Vector3& InMid, const Vector3& InTip, const Vector3& InTarget, const Vector3& InPole, bool InUsePole );

    // The direction InAim turned toward InDesired, but by at most InMaxAngleDegrees (<= 0: no limit).
    Vector3 LimitDirection( const Vector3& InAim, const Vector3& InDesired, float InMaxAngleDegrees );

    // Rotates InVector by InRotation.
    Vector3 Rotate( const Quaternion& InRotation, const Vector3& InVector );
    // InA then InB, as world rotations (InB * InA).
    Quaternion Then( const Quaternion& InA, const Quaternion& InB );
    // A fraction of a rotation (slerp from identity).
    Quaternion Scale( const Quaternion& InRotation, float InFraction );
}
