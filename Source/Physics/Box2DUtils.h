#pragma once
// Internal: conversions between engine math and Box2D. Only include from .cpp files.
// 2D physics lives in the world XY plane; rotation is the angle about +Z.
#include <box2d/box2d.h>
#include "Math/Quaternion.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Box2DUtils
{
    inline b2Vec2 ToB2( const Vector2& InVector )
    {
        return b2Vec2{ InVector.x, InVector.y };
    }

    inline b2Vec2 ToB2( const Vector3& InVector )
    {
        return b2Vec2{ InVector.x, InVector.y };
    }

    inline Vector2 FromB2( const b2Vec2& InVector )
    {
        return Vector2( InVector.x, InVector.y );
    }

    inline Vector3 FromB2( const b2Vec2& InVector, float InZ )
    {
        return Vector3( InVector.x, InVector.y, InZ );
    }

    // Rotation about +Z of a rotation (exact for pure Z rotations, the "yaw about Z" otherwise).
    inline float AngleOf( const Quaternion& InRotation )
    {
        return std::atan2( 2.f * ( InRotation.w * InRotation.z + InRotation.x * InRotation.y ), 1.f - 2.f * ( InRotation.y * InRotation.y + InRotation.z * InRotation.z ) );
    }

    inline Quaternion RotationOf( float InRadians )
    {
        return Quaternion( 0.f, 0.f, std::sin( InRadians * 0.5f ), std::cos( InRadians * 0.5f ) );
    }

    // InRotation turned about world Z so its AngleOf becomes InRadians; any X/Y tilt is kept
    // (a world-Z pre-rotation adds exactly to the ZYX yaw that AngleOf extracts).
    inline Quaternion WithAngle( const Quaternion& InRotation, float InRadians )
    {
        float delta = std::remainder( InRadians - AngleOf( InRotation ), 6.28318530718f );
        return RotationOf( delta ) * InRotation;
    }

    // Ids are stored in components as opaque 64-bit values (0 = none) so headers stay free of Box2D.
    template<typename Id>
    inline uint64_t Pack( Id InId )
    {
        static_assert( sizeof( Id ) <= sizeof( uint64_t ), "Box2D ids fit in 8 bytes" );
        uint64_t value = 0;
        std::memcpy( &value, &InId, sizeof( InId ) );
        return value;
    }

    template<typename Id>
    inline Id Unpack( uint64_t InValue )
    {
        Id id;
        std::memcpy( &id, &InValue, sizeof( id ) );
        return id;
    }

    inline b2BodyId Body( uint64_t InValue )
    {
        return Unpack<b2BodyId>( InValue );
    }

    inline bool IsBody( uint64_t InValue )
    {
        return InValue != 0 && b2Body_IsValid( Body( InValue ) );
    }
}
