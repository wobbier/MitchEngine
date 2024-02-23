#include "Quaternion.h"
#include <algorithm>
#include "Mathf.h"
#include <glm/gtc/quaternion.hpp>

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
