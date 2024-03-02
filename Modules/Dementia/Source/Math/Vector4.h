#pragma once
#include <string>
#include <glm/glm.hpp>
#include "LinearMath/btVector3.h"

struct Vector4
{
    union
    {
        struct
        {
            float x, y, z, w;
        };
        glm::vec4 InternalVector;
    };

    Vector4()
        : x( 0.f )
        , y( 0.f )
        , z( 0.f )
        , w( 0.f )
    {
    }

    Vector4( float X, float Y, float Z, float W )
        : x( X )
        , y( Y )
        , z( Z )
        , w( W )
    {
    }

    Vector4( float V )
        : x( V )
        , y( V )
        , z( V )
        , w( V )
    {
    }

    Vector4( const btVector4& v )
        : x( v.x() )
        , y( v.y() )
        , z( v.z() )
        , w( v.w() )
    {
    }

    Vector4( const glm::vec4& v )
        : x( v.x )
        , y( v.y )
        , z( v.z )
        , w( v.w )
    {
    }

    Vector4& operator=( const Vector4& other )
    {
        x = other.x;
        y = other.y;
        z = other.z;
        w = other.w;
        return *this;
    }

    float& operator[] ( int i )
    {
        return ( &x )[i];
    }

    const float& operator[] ( int i ) const
    {
        return ( &x )[i];
    }

    bool operator== ( const Vector4& v )
    {
        return x == v.x && y == v.y && z == v.z && w == v.w;
    }

    bool operator!= ( const Vector4& v )
    {
        return x != v.x || y != v.y || z != v.z && w != v.w;
    }

    Vector4& operator*= ( float s )
    {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }

    Vector4& operator/= ( float s )
    {
        s = 1.f / s;
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }

    Vector4& operator+= ( const Vector4& v )
    {
        x += v.x;
        y += v.y;
        z += v.z;
        w += v.w;
        return *this;
    }

    Vector4& operator-= ( const Vector4& v )
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        w -= v.w;
        return *this;
    }

    inline Vector4 operator* ( float s ) const
    {
        return Vector4( x * s, y * s, z * s, w * s );
    }

    inline Vector4 operator/ ( float s ) const
    {
        s = 1.f / s;
        return Vector4( x * s, y * s, z * s, w * s );
    }

    inline Vector4 operator- () const
    {
        return Vector4( -x, -y, -z, -w );
    }

    inline Vector4 operator+ ( const Vector4& b ) const
    {
        return Vector4( x + b.x, y + b.y, z + b.z, w + b.w );
    }

    inline Vector4 operator- ( const Vector4& b ) const
    {
        return Vector4( x - b.x, y - b.y, z - b.z, w - b.w );
    }

    inline bool IsZero() const
    {
        return ( x == 0.f && y == 0.f && z == 0.f && w == 0.f );
    }

    inline float Length() const
    {
        return std::sqrt( x * x + y * y + z * z + w * w );
    }

    inline float LengthSquared() const
    {
        float s = ( x * x + y * y + z * z + w * w );
        s *= s;
        return s;
    }

    inline Vector4& Normalize()
    {
        float m = Length();
        x /= m;
        y /= m;
        z /= m;
        w /= m;
        return *this;
    }

    inline Vector4 Normalized() const
    {
        Vector4 ret( x, y, z, w );
        return Vector4( ret / Length() );
    }

    inline float Dot( const Vector4& other ) const
    {
        return ( x * other.x + y * other.y + z * other.z + w * other.w );
    }
};