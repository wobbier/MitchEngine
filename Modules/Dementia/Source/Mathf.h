#pragma once
#include "Math/Vector3.h"
#include <Math/Vector2.h>
#include <utility>

namespace Mathf
{
    inline float Clamp( float Min, float Max, float Val )
    {
        if ( Val < Min )
        {
            return Min;
        }
        if ( Val > Max )
        {
            return Max;
        }
        return Val;
    }

    inline float Abs( float input )
    {
        if ( input < 0.f )
        {
            input *= -1.f;
        }
        return input;
    }

    inline float Lerp( float v0, float v1, float t )
    {
        return ( 1.f - t ) * v0 + t * v1;
    }

    inline Vector3 Lerp( const Vector3& start, const Vector3& end, float percent )
    {
        return ( start + ( end - start ) * percent );
    }

    inline float Round( float number )
    {
        return std::round( number );
    }

    inline constexpr float Radians( float number )
    {
        return glm::radians( number );
    }

    inline float Degrees( float rads )
    {
        return glm::degrees( rads );
    }

    template<class T>
    inline auto Sign( T inSign )
    {
        if ( inSign > 0 ) return 1;
        if ( inSign < 0 ) return -1;
        return 0;
    }

    inline Vector2 KeepAspect( const Vector2& inSize, const Vector2& inSizeConstraints )
    {
        const float bestRatio = std::min( inSizeConstraints.x / inSize.x, inSizeConstraints.y / inSize.y );
        return { inSize.x * bestRatio, inSize.y * bestRatio };
    }
}
