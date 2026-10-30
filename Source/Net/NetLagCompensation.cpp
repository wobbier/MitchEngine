#include "PCH.h"
#include "Net/NetLagCompensation.h"
#include "Components/Networking/NetHitbox.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "Debug/DebugDraw.h"
#include "Engine/World.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Ray vs sphere: nearest t >= 0, or -1.
    float RaySphere( const Vector3& InOrigin, const Vector3& InDirection, const Vector3& InCenter, float InRadius )
    {
        const Vector3 m = InOrigin - InCenter;
        const float b = m.Dot( InDirection );
        const float c = m.Dot( m ) - InRadius * InRadius;
        if( c > 0.f && b > 0.f )
        {
            return -1.f;
        }
        const float discriminant = b * b - c;
        if( discriminant < 0.f )
        {
            return -1.f;
        }
        return std::max( 0.f, -b - std::sqrt( discriminant ) );
    }

    // Ray vs capsule (segment A-B, radius): the infinite cylinder clipped to the segment, then
    // the end spheres.
    float RayCapsule( const Vector3& InOrigin, const Vector3& InDirection, const Vector3& InA, const Vector3& InB, float InRadius )
    {
        const Vector3 axis = InB - InA;
        const float axisLength2 = axis.Dot( axis );
        float best = -1.f;
        auto consider = [&best]( float InT ) {
            if( InT >= 0.f && ( best < 0.f || InT < best ) )
            {
                best = InT;
            }
        };
        if( axisLength2 > 1e-12f )
        {
            const Vector3 m = InOrigin - InA;
            const float md = m.Dot( axis );
            const float nd = InDirection.Dot( axis );
            const float a = axisLength2 - nd * nd;
            const float k = m.Dot( m ) - InRadius * InRadius;
            const float c = axisLength2 * k - md * md;
            if( std::abs( a ) > 1e-9f )
            {
                const float b = axisLength2 * m.Dot( InDirection ) - nd * md;
                const float discriminant = b * b - a * c;
                if( discriminant >= 0.f )
                {
                    const float t = ( -b - std::sqrt( discriminant ) ) / a;
                    const float along = md + t * nd;
                    if( along >= 0.f && along <= axisLength2 )
                    {
                        consider( c <= 0.f && along >= 0.f && along <= axisLength2 ? 0.f : t );
                    }
                }
            }
        }
        consider( RaySphere( InOrigin, InDirection, InA, InRadius ) );
        consider( RaySphere( InOrigin, InDirection, InB, InRadius ) );
        return best;
    }

    // Ray vs oriented box (slabs in the box's frame).
    float RayBox( const Vector3& InOrigin, const Vector3& InDirection, const Vector3& InCenter, const Quaternion& InRotation, const Vector3& InHalf )
    {
        const Quaternion inverse = InRotation.Inverse();
        const Vector3 origin = inverse * ( InOrigin - InCenter );
        const Vector3 direction = inverse * InDirection;
        float near = 0.f;
        float far = 1e30f;
        for( int axis = 0; axis < 3; ++axis )
        {
            if( std::abs( direction[axis] ) < 1e-9f )
            {
                if( std::abs( origin[axis] ) > InHalf[axis] )
                {
                    return -1.f;
                }
                continue;
            }
            float t0 = ( -InHalf[axis] - origin[axis] ) / direction[axis];
            float t1 = ( InHalf[axis] - origin[axis] ) / direction[axis];
            if( t0 > t1 )
            {
                std::swap( t0, t1 );
            }
            near = std::max( near, t0 );
            far = std::min( far, t1 );
            if( near > far )
            {
                return -1.f;
            }
        }
        return near;
    }

    // A sphere around the pose's shape (a box's corners, a capsule's caps).
    float BoundingRadius( const NetLagCompensation::HitboxPose& InPose )
    {
        return InPose.Shape == static_cast<uint8_t>( NetHitboxShape::Box ) ? InPose.HalfExtents.Length() : InPose.HalfHeight + InPose.Radius;
    }

    // Ray (unit direction) vs AABB within InMaxDistance.
    bool RayHitsBounds( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const Vector3& InMin, const Vector3& InMax )
    {
        float near = 0.f;
        float far = InMaxDistance;
        for( int axis = 0; axis < 3; ++axis )
        {
            if( std::abs( InDirection[axis] ) < 1e-9f )
            {
                if( InOrigin[axis] < InMin[axis] || InOrigin[axis] > InMax[axis] )
                {
                    return false;
                }
                continue;
            }
            float t0 = ( InMin[axis] - InOrigin[axis] ) / InDirection[axis];
            float t1 = ( InMax[axis] - InOrigin[axis] ) / InDirection[axis];
            if( t0 > t1 )
            {
                std::swap( t0, t1 );
            }
            near = std::max( near, t0 );
            far = std::min( far, t1 );
            if( near > far )
            {
                return false;
            }
        }
        return true;
    }

    NetLagCompensation::HitboxPose Lerp( const NetLagCompensation::HitboxPose& InA, const NetLagCompensation::HitboxPose& InB, float InT )
    {
        NetLagCompensation::HitboxPose pose = InA;
        pose.Position = InA.Position + ( InB.Position - InA.Position ) * InT;
        pose.Rotation = Quaternion::Slerp( InA.Rotation, InB.Rotation, InT );
        return pose;
    }
}


void NetLagCompensation::Record( World& InWorld, uint32_t InTick )
{
    Frame frame;
    frame.Tick = InTick;
    InWorld.ForEachEntity( [&]( Entity& InEntity ) {
        const NetHitbox* hitbox = InEntity.TryGetComponent<NetHitbox>();
        Transform* transform = InEntity.TryGetComponent<Transform>();
        if( !hitbox || !transform || !hitbox->IsEnabled() )
        {
            return;
        }
        HitboxPose pose;
        pose.Entity = InWorld.GetEntity( InEntity.GetId() );
        pose.Shape = static_cast<uint8_t>( hitbox->Shape );
        pose.Rotation = transform->GetWorldRotation();
        pose.Position = transform->GetLocalToWorldMatrix().TransformPoint( hitbox->Center );
        pose.Radius = hitbox->Radius;
        pose.HalfHeight = std::max( 0.f, hitbox->Height * 0.5f - hitbox->Radius );
        pose.HalfExtents = hitbox->HalfExtents;
        pose.Occluder = hitbox->Occluder;
        frame.Hitboxes.push_back( pose );
    } );
    m_frames.push_back( std::move( frame ) );
    const size_t keep = static_cast<size_t>( std::ceil( MaxRewindSeconds * m_tickRate ) ) + 2;
    while( m_frames.size() > keep )
    {
        m_frames.pop_front();
    }

    // NET-15: each hitbox's bounds over the window, so a shot only rewinds what it could touch.
    m_swept.clear();
    for( const Frame& recorded : m_frames )
    {
        for( const HitboxPose& pose : recorded.Hitboxes )
        {
            const float radius = BoundingRadius( pose );
            const Vector3 low = pose.Position - Vector3( radius, radius, radius );
            const Vector3 high = pose.Position + Vector3( radius, radius, radius );
            auto [it, inserted] = m_swept.try_emplace( pose.Entity.GetID().Value(), Bounds{ low, high } );
            if( !inserted )
            {
                for( int axis = 0; axis < 3; ++axis )
                {
                    it->second.Min[axis] = std::min( it->second.Min[axis], low[axis] );
                    it->second.Max[axis] = std::max( it->second.Max[axis], high[axis] );
                }
            }
        }
    }
}


std::vector<NetLagCompensation::HitboxPose> NetLagCompensation::GetPosesAt( double InTick ) const
{
    return PosesAt( InTick, nullptr );
}


std::vector<NetLagCompensation::HitboxPose> NetLagCompensation::PosesAt( double InTick, const std::unordered_set<uint64_t>* InOnly ) const
{
    if( m_frames.empty() )
    {
        return {};
    }
    const double tick = std::clamp( InTick, static_cast<double>( m_frames.front().Tick ), static_cast<double>( m_frames.back().Tick ) );
    const Frame* before = &m_frames.front();
    const Frame* after = &m_frames.front();
    for( const Frame& frame : m_frames )
    {
        if( frame.Tick <= tick )
        {
            before = &frame;
            after = &frame;
        }
        else
        {
            after = &frame;
            break;
        }
    }
    const float t = after->Tick > before->Tick ? static_cast<float>( ( tick - before->Tick ) / ( after->Tick - before->Tick ) ) : 0.f;
    std::vector<HitboxPose> poses;
    for( const HitboxPose& pose : before->Hitboxes )
    {
        if( InOnly && !InOnly->count( pose.Entity.GetID().Value() ) )
        {
            continue;
        }
        auto match = std::find_if( after->Hitboxes.begin(), after->Hitboxes.end(), [&pose]( const HitboxPose& InOther ) { return InOther.Entity == pose.Entity; } );
        poses.push_back( match != after->Hitboxes.end() ? Lerp( pose, *match, t ) : pose );
    }
    return poses;
}


float NetLagCompensation::Intersect( const HitboxPose& InPose, const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance )
{
    float t = -1.f;
    if( InPose.Shape == static_cast<uint8_t>( NetHitboxShape::Box ) )
    {
        t = RayBox( InOrigin, InDirection, InPose.Position, InPose.Rotation, InPose.HalfExtents );
    }
    else
    {
        const Vector3 up = InPose.Rotation * Vector3( 0.f, InPose.HalfHeight, 0.f );
        t = RayCapsule( InOrigin, InDirection, InPose.Position - up, InPose.Position + up, InPose.Radius );
    }
    return t >= 0.f && t <= InMaxDistance ? t : -1.f;
}


LagCompensatedHit NetLagCompensation::Raycast( PhysicsCore* InPhysics, const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, double InViewTick,
    const EntityHandle& InIgnore ) const
{
    LagCompensatedHit result;
    const double newest = GetNewestTick();
    double viewTick = InViewTick < 0.0 ? newest : InViewTick;
    if( newest - viewTick < -1.0 )
    {
        result.Rejected = true;
        return result;
    }
    // High ping (NET-15): a view older than the cap is judged at the cap, not refused. A target
    // isn't pulled back behind cover further than that; a shot that misses there simply misses.
    const double maxTicks = MaxRewindSeconds * m_tickRate;
    if( newest - viewTick > maxTicks + 1e-6 )
    {
        viewTick = newest - maxTicks;
        result.Clamped = true;
    }
    result.RewoundTicks = newest - viewTick;
    const float length = InDirection.Length();
    if( length < 1e-9f )
    {
        return result;
    }
    const Vector3 direction = InDirection * ( 1.f / length );

    // What the shooter saw: the hitboxes whose swept bounds the ray crosses, rewound.
    std::unordered_set<uint64_t> candidates;
    if( UseBroadphase )
    {
        for( const auto& [id, bounds] : m_swept )
        {
            if( RayHitsBounds( InOrigin, direction, InMaxDistance, bounds.Min, bounds.Max ) )
            {
                candidates.insert( id );
            }
        }
    }
    const std::vector<HitboxPose> poses = PosesAt( viewTick, UseBroadphase ? &candidates : nullptr );
    float nearest = -1.f;
    // Moving blockers (Occluder hitboxes) block where they were in the shooter's view (NET-15).
    float occluder = -1.f;
    for( const HitboxPose& pose : poses )
    {
        if( InIgnore && pose.Entity == InIgnore )
        {
            continue;
        }
        ++result.Tested;
        const float t = Intersect( pose, InOrigin, direction, InMaxDistance );
        if( t < 0.f )
        {
            continue;
        }
        if( pose.Occluder )
        {
            occluder = occluder < 0.f ? t : std::min( occluder, t );
        }
        else if( nearest < 0.f || t < nearest )
        {
            nearest = t;
            result.Entity = pose.Entity;
        }
    }

    // The present world's colliders block it too (hitbox owners' own bodies, occluders' included,
    // are where they are now, not where they were: they don't count).
    if( InPhysics )
    {
        for( const RaycastHit& hit : InPhysics->RaycastAll( InOrigin, direction, InMaxDistance ) )
        {
            if( hit.Entity && ( hit.Entity->HasComponent<NetHitbox>() || ( InIgnore && hit.Entity == InIgnore ) ) )
            {
                continue;
            }
            if( occluder < 0.f || hit.Distance < occluder )
            {
                occluder = hit.Distance;
            }
        }
    }
    if( nearest >= 0.f )
    {
        result.Occluded = occluder >= 0.f && occluder < nearest;
        result.Hit = !result.Occluded;
        result.Distance = nearest;
        result.Point = InOrigin + direction * nearest;
    }

    if( DrawOverlays )
    {
        const Vector3 end = InOrigin + direction * ( result.Hit ? nearest : ( occluder >= 0.f ? occluder : InMaxDistance ) );
        DebugDraw::Line( InOrigin, end, result.Hit ? DebugDraw::Green : DebugDraw::Red, 2.f );
        auto draw = []( const HitboxPose& InPose, const Vector4& InColor ) {
            const Vector3 up = InPose.Rotation * Vector3( 0.f, InPose.HalfHeight, 0.f );
            DebugDraw::Capsule( InPose.Position - up, InPose.Position + up, InPose.Radius, InColor, 2.f );
        };
        for( const HitboxPose& pose : GetPosesAt( viewTick ) )
        {
            draw( pose, DebugDraw::Yellow );
        }
        for( const HitboxPose& pose : GetPosesAt( newest ) )
        {
            draw( pose, DebugDraw::Gray );
        }
    }
    return result;
}
