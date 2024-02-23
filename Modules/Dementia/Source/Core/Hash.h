#pragma once
#include <stdint.h>

namespace Hash
{
    namespace FNV1a
    {
        constexpr uint32_t const kConstValue32 = 0x811C9DC5;
        constexpr uint32_t const kDefaultOffsetBasis32 = 0x1000193;
        constexpr uint64_t const kConstValue64 = 0xCBF29CE484222325;
        constexpr uint64_t const kDefaultOffsetBasis64 = 0x100000001B3;

        constexpr static inline uint32_t GetHash32( char const* const inString, const uint32_t inValue = kConstValue32 )
        {
            return ( inString[0] == '\0' ) ? inValue : GetHash32( &inString[1], ( (uint64_t)inValue ^ uint64_t( inString[0] ) ) * kDefaultOffsetBasis32 );
        }

        constexpr static inline uint64_t GetHash64( char const* const inString, const uint64_t inValue = kConstValue64 )
        {
            return ( inString[0] == '\0' ) ? inValue : GetHash64( &inString[1], ( (uint64_t)inValue ^ uint64_t( inString[0] ) ) * kDefaultOffsetBasis32 );
        }
    }
}