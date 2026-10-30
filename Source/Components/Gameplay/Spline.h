#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"
#include <vector>

// The curve math, shared with the C# Spline (ScriptCore SplineMath.cs, kept in step): a uniform
// Catmull-Rom curve through every point. Open curves extrapolate their end tangents (the point
// before the first mirrors the second), closed ones wrap. t runs 0..1 over the whole curve, each
// segment an equal share.
namespace SplineMath
{
    int GetSegmentCount( size_t InPointCount, bool InClosed );
    Vector3 Evaluate( const std::vector<Vector3>& InPoints, bool InClosed, float InT );
    // dP/dt (t over the whole curve).
    Vector3 Derivative( const std::vector<Vector3>& InPoints, bool InClosed, float InT );
}

// GAME-2: a path through points in the entity's local space, for camera paths, rails, roads and
// patrols. Edited in the World view (drag a point's handle; one undo step per drag) and read from
// C# (ScriptCore Spline). Lengths and distances are in world units.
class Spline
    : public Component<Spline>
{
    ME_REFLECTABLE( Spline )
public:
    Spline();

    std::vector<Vector3> Points;
    bool Closed = false;

    Vector3 GetLocalPoint( float InT ) const;
    // World space, through the entity's transform.
    Vector3 GetPoint( float InT ) const;
    Vector3 GetTangent( float InT ) const;   // normalized
    Vector3 GetWorldControlPoint( size_t InIndex ) const;
    float GetLength() const;
    // The t a distance along the curve reaches (closed curves wrap; open ones clamp).
    float GetTAtDistance( float InDistance ) const;
    // The t of the curve's point nearest a world position.
    float GetNearestT( const Vector3& InWorld ) const;

private:
    Vector3 ToWorld( const Vector3& InLocal ) const;
    // Cumulative world lengths at evenly spaced t (kSamplesPerSegment per segment).
    std::vector<float> BuildLengthTable() const;
};
ME_REGISTER_COMPONENT_FOLDER( Spline, "Gameplay" )
