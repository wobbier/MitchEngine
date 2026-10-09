#pragma once
// Internal: hashing used by the physics cores to notice component / transform edits.
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace PhysicsHash
{
    inline uint64_t Mix( uint64_t InHash, uint64_t InValue )
    {
        return InHash ^ ( InValue + 0x9e3779b97f4a7c15ULL + ( InHash << 6 ) + ( InHash >> 2 ) );
    }

    inline uint64_t Bits( float InValue )
    {
        uint32_t bits = 0;
        std::memcpy( &bits, &InValue, sizeof( bits ) );
        return bits;
    }

    // Quantized so float noise from recomputed world transforms never looks like an edit.
    inline uint64_t Quantized( float InValue )
    {
        return static_cast<uint64_t>( static_cast<int64_t>( std::lround( InValue * 10000.f ) ) );
    }

    inline uint64_t MixVector( uint64_t InHash, const Vector3& InVector, bool InQuantize = false )
    {
        if( InQuantize )
        {
            return Mix( Mix( Mix( InHash, Quantized( InVector.x ) ), Quantized( InVector.y ) ), Quantized( InVector.z ) );
        }
        return Mix( Mix( Mix( InHash, Bits( InVector.x ) ), Bits( InVector.y ) ), Bits( InVector.z ) );
    }

    inline uint64_t MixVector( uint64_t InHash, const Vector2& InVector, bool InQuantize = false )
    {
        if( InQuantize )
        {
            return Mix( Mix( InHash, Quantized( InVector.x ) ), Quantized( InVector.y ) );
        }
        return Mix( Mix( InHash, Bits( InVector.x ) ), Bits( InVector.y ) );
    }
}
