#pragma once
#include "Vector3.h"
#include <glm/gtc/quaternion.hpp>

struct Quaternion
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
            float w;
        };
        glm::quat InternalQuat;
    };

    static constexpr float kEpsilon = 0.000001f;
    static Quaternion Identity;

    Quaternion()
        : x( 0.f ), y( 0.f ), z( 0.f ), w( 1.f )
    {
    }

    Quaternion( float X, float Y, float Z, float W )
        : x( X ), y( Y ), z( Z ), w( W )
    {
    }

    Quaternion( const Vector3& v, float W )
        : x( v.x ), y( v.y ), z( v.z ), w( W )
    {
    }

    Quaternion( const glm::quat& q )
        : x( q.x ), y( q.y ), z( q.z ), w( q.w )
    {
    }

    /// Euler Angles
    Quaternion( const Vector3& v )
        : InternalQuat( v.InternalVector )
    {
    }

    float& operator[]( int index )
    {
        return ( &x )[index];
    }

    const float& operator[]( int index ) const
    {
        return ( &x )[index];
    }

    Quaternion operator*( const Quaternion& rhs ) const
    {
        return Quaternion(
            w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
            w * rhs.y + y * rhs.w + z * rhs.x - x * rhs.z,
            w * rhs.z + z * rhs.w + x * rhs.y - y * rhs.x,
            w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z );
    }

    Vector3 operator*( const Vector3& point ) const
    {
        float qx = x * 2.f;
        float qy = y * 2.f;
        float qz = z * 2.f;
        float xx = qx * x;
        float yy = qy * y;
        float zz = qz * z;
        float xy = qx * y;
        float xz = qx * z;
        float yz = qy * z;
        float wx = w * x;
        float wy = w * y;
        float wz = w * z;

        Vector3 res;
        res.x = ( 1.f - ( yy + zz ) ) * point.x + ( xy - wz ) * point.y + ( xz + wy ) * point.z;
        res.y = ( xy + wz ) * point.x + ( 1.f - ( xx + zz ) ) * point.y + ( yz - wx ) * point.z;
        res.z = ( xz - wy ) * point.x + ( yz + wx ) * point.y + ( 1.f - ( xx + yy ) ) * point.z;
        return res;
    }

    Quaternion& operator=( const Quaternion& rhs )
    {
        x = rhs.x;
        y = rhs.y;
        z = rhs.z;
        w = rhs.w;

        return *this;
    }

    //Vector3 operator*(const Quaternion& rotation, const Vector3& point)

    bool IsEqualUsingDot( float dot ) const
    {
        return dot > 1.f - kEpsilon;
    }

    bool operator==( const Quaternion& rhs ) const
    {
        return IsEqualUsingDot( Dot( rhs ) );
    }

    bool operator!=( const Quaternion& rhs ) const
    {
        return !IsEqualUsingDot( Dot( rhs ) );
    }

    float Dot( const Quaternion& other ) const
    {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    void SetLookRotation( const Vector3& view, const Vector3& up = Vector3::Up )
    {
        *this = LookRotation( view, up );
    }

    // Rotation whose Front (+Z, left-handed) points along forward, with up as the up hint.
    static Quaternion LookRotation( const Vector3& forward, const Vector3& up = Vector3::Up );

    // Rotation of `radians` around `axis` (normalized internally).
    static Quaternion AngleAxis( float radians, const Vector3& axis );

    // Euler angles in degrees, same convention as ToEulerAngles.
    static Quaternion FromEulerDegrees( const Vector3& degrees );

    // Spherical interpolation along the shortest arc.
    static Quaternion Slerp( const Quaternion& a, const Quaternion& b, float t );

    // Angle between two rotations in radians.
    static float Angle( const Quaternion& a, const Quaternion& b );

    Vector3 Rotate( const Vector3& inVector ) const
    {
        float vMult = 2.f * ( x * inVector.x + y * inVector.y + z * inVector.z );
        float crossMult = 2.f * w;
        float pMult = crossMult * w - 1.f;

        return Vector3( pMult * inVector.x + vMult * x + crossMult * ( y * inVector.z - z * inVector.y ),
            pMult * inVector.y + vMult * y + crossMult * ( z * inVector.x - x * inVector.z ),
            pMult * inVector.z + vMult * z + crossMult * ( x * inVector.y - y * inVector.x ) );
    }

    Quaternion Inverse() const
    {
        return Quaternion( glm::inverse( InternalQuat ) );
    }

    const Vector3& GetVectorPart() const
    {
        return reinterpret_cast<const Vector3&>( x );
    }

    void SetEuler( const Vector3& euler );

    Quaternion Normalized() const
    {
        float mag = std::sqrt( Dot( *this ) );

        if( mag < kEpsilon )
        {
            return Identity;
        }

        return Quaternion( x / mag, y / mag, z / mag, w / mag );
    }

    void Normalize()
    {
        Quaternion q = Normalized();
        x = q.x;
        y = q.y;
        z = q.z;
        w = q.w;
    }

    //Matrix3 GetRotationMatrix();
    //void SetRotationMatrix(const Matrix3& m);

    static Vector3 ToEulerAngles( const Quaternion& InQuat );

    float ToAngle() const;
    Vector3 ToAxis() const;

};

//class Quaternion
//{
//public:
//	Quaternion()
//	{
//
//	}
//	Quaternion(const Vector3& Deg)
//		: m_quat(DirectX::XMQuaternionRotationRollPitchYawFromVector(Deg.GetInternalVec()))
//	{
//
//	}
//	Quaternion(const DirectX::SimpleMath::Quaternion& Quat)
//		: m_quat(Quat)
//	{
//
//	}
//	~Quaternion()
//	{
//
//	}
//
//	float& operator[](int index)
//	{
//		switch (index)
//		{
//		case 0:
//			return m_quat.x;
//		case 1:
//			return m_quat.y;
//		case 2:
//			return m_quat.z;
//		default:
//			return m_quat.w;
//		}
//	}
//
//	const float& operator[](int index) const
//	{
//		switch (index)
//		{
//		case 0:
//			return m_quat.x;
//		case 1:
//			return m_quat.y;
//		case 2:
//			return m_quat.z;
//		default:
//			return m_quat.w;
//		}
//	}
//
//	Vector3 operator*(const Vector3& InPoint)
//	{
//		float num = m_quat.x * 2.f;
//		float num2 = m_quat.y * 2.f;
//		float num3 = m_quat.z * 2.f;
//		float num4 = m_quat.x * num;
//		float num5 = m_quat.y * num2;
//		float num6 = m_quat.z * num3;
//		float num7 = m_quat.x * num2;
//		float num8 = m_quat.x * num3;
//		float num9 = m_quat.y * num3;
//		float num10 = m_quat.w * num;
//		float num11 = m_quat.w * num2;
//		float num12 = m_quat.w * num3;
//		Vector3 result;
//		result[0] = (1.f - (num5 + num6)) * InPoint[0] + (num7 - num12) * InPoint[1] + (num8 + num11) * InPoint[2];
//		result[1] = (num7 + num12) * InPoint[0] + (1.f - (num4 + num6)) * InPoint[1] + (num9 - num10) * InPoint[2];
//		result[2] = (num8 - num11) * InPoint[0] + (num9 + num10) * InPoint[1] + (1.f - (num4 + num5)) * InPoint[2];
//		return result;
//	}
//
//	static Vector3 ToEulerAngles(Quaternion InQuat);
//
//	static float Angle(Quaternion& a, Quaternion& b);
//
//	static Quaternion AngleAxis(float InDegrees, const Vector3& InAxis);
//
//	void Normalize();
//	Quaternion Normalized();
//
//	DirectX::SimpleMath::Quaternion& GetInternalVec()
//	{
//		return m_quat;
//	}
//private:
//	DirectX::SimpleMath::Quaternion m_quat;
//};
