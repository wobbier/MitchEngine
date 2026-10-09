#pragma once
// Internal: conversions between engine math and Box3D. Only include from .cpp files.
#include <box3d/box3d.h>
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <cstdint>
#include <cstring>

namespace Box3DUtils
{
    inline b3Vec3 ToB3( const Vector3& InVector )
    {
        return b3Vec3{ InVector.x, InVector.y, InVector.z };
    }

    inline Vector3 FromB3( const b3Vec3& InVector )
    {
        return Vector3( InVector.x, InVector.y, InVector.z );
    }

    inline b3Quat ToB3( const Quaternion& InRotation )
    {
        return b3Quat{ b3Vec3{ InRotation.x, InRotation.y, InRotation.z }, InRotation.w };
    }

    inline Quaternion FromB3( const b3Quat& InRotation )
    {
        return Quaternion( InRotation.v.x, InRotation.v.y, InRotation.v.z, InRotation.s );
    }

    // Ids are stored in components as opaque 64-bit values (0 = none) so headers stay free of Box3D.
    template<typename Id>
    inline uint64_t Pack( Id InId )
    {
        static_assert( sizeof( Id ) == sizeof( uint64_t ), "Box3D ids are 8 bytes" );
        uint64_t value = 0;
        std::memcpy( &value, &InId, sizeof( value ) );
        return value;
    }

    template<typename Id>
    inline Id Unpack( uint64_t InValue )
    {
        Id id;
        std::memcpy( &id, &InValue, sizeof( id ) );
        return id;
    }

    inline b3BodyId Body( uint64_t InValue )
    {
        return Unpack<b3BodyId>( InValue );
    }

    inline bool IsBody( uint64_t InValue )
    {
        return InValue != 0 && b3Body_IsValid( Body( InValue ) );
    }
}
