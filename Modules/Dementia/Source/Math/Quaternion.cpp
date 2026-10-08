#include "Quaternion.h"
#include <algorithm>
#include "Mathf.h"
#include <glm/gtc/quaternion.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#define _USE_MATH_DEFINES
#include <math.h>

Quaternion Quaternion::Identity = Quaternion( 0.f, 0.f, 0.f, 1.f );

void Quaternion::SetEuler( const Vector3& euler )
{
    InternalQuat = glm::quat( Vector3( Mathf::Radians( euler.x ), Mathf::Radians( euler.y ), Mathf::Radians( euler.z ) ).InternalVector );
}

Vector3 Quaternion::ToEulerAngles( const Quaternion& InQuat )
{
    Vector3 angles;
    angles.InternalVector = glm::eulerAngles( InQuat.InternalQuat );
    angles.x = Mathf::Degrees( angles.x );
    angles.y = Mathf::Degrees( angles.y );
    angles.z = Mathf::Degrees( angles.z );
    return angles;
}

Quaternion Quaternion::LookRotation( const Vector3& forward, const Vector3& up )
{
    const glm::vec3 direction = glm::normalize( forward.InternalVector );
    glm::vec3 upHint = up.InternalVector;
    // Pick another up axis when looking (anti-)parallel to it.
    if( std::abs( glm::dot( direction, glm::normalize( upHint ) ) ) > 0.9999f )
    {
        upHint = std::abs( direction.y ) < 0.9f ? glm::vec3( 0.f, 1.f, 0.f ) : glm::vec3( 1.f, 0.f, 0.f );
    }
    return Quaternion( glm::quatLookAtLH( direction, upHint ) );
}


Quaternion Quaternion::AngleAxis( float radians, const Vector3& axis )
{
    return Quaternion( glm::angleAxis( radians, glm::normalize( axis.InternalVector ) ) );
}


Quaternion Quaternion::FromEulerDegrees( const Vector3& degrees )
{
    return Quaternion( glm::quat( glm::vec3( Mathf::Radians( degrees.x ), Mathf::Radians( degrees.y ), Mathf::Radians( degrees.z ) ) ) );
}


Quaternion Quaternion::Slerp( const Quaternion& a, const Quaternion& b, float t )
{
    return Quaternion( glm::slerp( a.InternalQuat, b.InternalQuat, t ) );
}


float Quaternion::Angle( const Quaternion& a, const Quaternion& b )
{
    const float d = std::min( std::abs( a.Dot( b ) ), 1.f );
    return 2.f * std::acos( d );
}


float Quaternion::ToAngle() const
{
    return glm::angle( InternalQuat );
}

Vector3 Quaternion::ToAxis() const
{
    glm::vec3 axis = glm::axis( InternalQuat );
    return Vector3( axis );
}

//
//float Quaternion::Angle(Quaternion& a, Quaternion& b)
//{
//	float f = a.GetInternalVec().Dot(b.GetInternalVec());
//
//	return std::acos(std::min(std::abs(f), 1.f)) * 2.f * 57.29578f;
//}
//
//Quaternion Quaternion::AngleAxis(float InDegrees, const Vector3& InAxis)
//{
//	//if (InAxis.LengthSquared() == 0.0f)
//	//	return Quaternion(DirectX::SimpleMath::Quaternion::Identity);
//
//	//Quaternion result = DirectX::SimpleMath::Quaternion::Identity;
//	//float radians = Mathf::Radians(InDegrees);
//	//radians *= 0.5f;
//
//	//Vector3 normalizedAxis = InAxis.Normalized();
//	//normalizedAxis = normalizedAxis * (float)std::sin(radians);
//	//result[0] = normalizedAxis.X();
//	//result[1] = normalizedAxis.Y();
//	//result[2] = normalizedAxis.Z();
//	//result[3] = (float)std::cos(radians);
//
//	//return result.Normalized();
//	return Quaternion(DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(InAxis.GetInternalVec(), Mathf::Radians(InDegrees)));
//}
//
//void Quaternion::Normalize()
//{
//	m_quat.Normalize(m_quat);
//}
//
//Quaternion Quaternion::Normalized()
//{
//	DirectX::SimpleMath::Quaternion tempQuat;
//	m_quat.Normalize(tempQuat);
//	return Quaternion(tempQuat);
//}
