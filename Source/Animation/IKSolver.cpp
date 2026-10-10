#include "PCH.h"
#include "IKSolver.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace IK
{
    namespace
    {
        // Any unit vector perpendicular to InDirection.
        glm::vec3 AnyPerpendicular( const glm::vec3& InDirection )
        {
            const glm::vec3 axis = std::abs( InDirection.y ) < 0.9f ? glm::vec3( 0.f, 1.f, 0.f ) : glm::vec3( 1.f, 0.f, 0.f );
            return glm::normalize( glm::cross( InDirection, axis ) );
        }


        glm::quat FromToGlm( const glm::vec3& InFrom, const glm::vec3& InTo )
        {
            const float fromLength = glm::length( InFrom );
            const float toLength = glm::length( InTo );
            if( fromLength < 1e-8f || toLength < 1e-8f )
            {
                return glm::quat( 1.f, 0.f, 0.f, 0.f );
            }
            const glm::vec3 from = InFrom / fromLength;
            const glm::vec3 to = InTo / toLength;
            const float cosine = glm::dot( from, to );
            if( cosine < -0.999999f )
            {
                // Opposite: half a turn about any perpendicular axis.
                return glm::angleAxis( glm::pi<float>(), AnyPerpendicular( from ) );
            }
            const glm::vec3 axis = glm::cross( from, to );
            return glm::normalize( glm::quat( 1.f + cosine, axis.x, axis.y, axis.z ) );
        }
    }


    Quaternion FromTo( const Vector3& InFrom, const Vector3& InTo )
    {
        return Quaternion( FromToGlm( InFrom.InternalVector, InTo.InternalVector ) );
    }


    Vector3 Rotate( const Quaternion& InRotation, const Vector3& InVector )
    {
        return Vector3( InRotation.InternalQuat * InVector.InternalVector );
    }


    Quaternion Then( const Quaternion& InA, const Quaternion& InB )
    {
        return Quaternion( glm::normalize( InB.InternalQuat * InA.InternalQuat ) );
    }


    Quaternion Scale( const Quaternion& InRotation, float InFraction )
    {
        return Quaternion( glm::slerp( glm::quat( 1.f, 0.f, 0.f, 0.f ), InRotation.InternalQuat, std::clamp( InFraction, 0.f, 1.f ) ) );
    }


    TwoBoneSolution SolveTwoBone( const Vector3& InRoot, const Vector3& InMid, const Vector3& InTip, const Vector3& InTarget, const Vector3& InPole, bool InUsePole )
    {
        TwoBoneSolution solution;
        const glm::vec3 a = InRoot.InternalVector;
        const glm::vec3 b = InMid.InternalVector;
        const glm::vec3 c = InTip.InternalVector;
        const float upper = glm::length( b - a );
        const float lower = glm::length( c - b );
        const glm::vec3 toTarget = InTarget.InternalVector - a;
        const float targetDistance = glm::length( toTarget );
        if( upper < 1e-6f || lower < 1e-6f || targetDistance < 1e-6f )
        {
            return solution;
        }
        const glm::vec3 direction = toTarget / targetDistance;
        const float reach = upper + lower;
        const float epsilon = reach * 1e-4f;
        solution.Reached = targetDistance <= reach;
        const float distance = std::clamp( targetDistance, std::abs( upper - lower ) + epsilon, reach - epsilon );

        // The bend plane: toward the pole, else the chain's current bend.
        glm::vec3 bend = InUsePole ? InPole.InternalVector - a : b - a;
        bend -= direction * glm::dot( bend, direction );
        if( glm::dot( bend, bend ) < 1e-10f )
        {
            // Pole on the line to the target (or a straight chain): try the lower bone's offset.
            bend = ( b - a ) + ( b - c );
            bend -= direction * glm::dot( bend, direction );
        }
        const glm::vec3 bendDirection = glm::dot( bend, bend ) < 1e-10f ? AnyPerpendicular( direction ) : glm::normalize( bend );

        // Law of cosines: the mid joint's distance along the line to the target, and off it.
        const float along = ( upper * upper - lower * lower + distance * distance ) / ( 2.f * distance );
        const float off = std::sqrt( std::max( upper * upper - along * along, 0.f ) );
        const glm::vec3 mid = a + direction * along + bendDirection * off;
        const glm::vec3 tip = a + direction * distance;

        const glm::quat rootDelta = FromToGlm( b - a, mid - a );
        const glm::vec3 tipAfterRoot = a + rootDelta * ( c - a );
        const glm::quat midDelta = FromToGlm( tipAfterRoot - mid, tip - mid );
        solution.RootDelta = Quaternion( rootDelta );
        solution.MidDelta = Quaternion( midDelta );
        return solution;
    }


    Vector3 LimitDirection( const Vector3& InAim, const Vector3& InDesired, float InMaxAngleDegrees )
    {
        const glm::vec3 aim = glm::normalize( InAim.InternalVector );
        const glm::vec3 desired = glm::normalize( InDesired.InternalVector );
        if( InMaxAngleDegrees <= 0.f )
        {
            return Vector3( desired );
        }
        const float angle = std::acos( std::clamp( glm::dot( aim, desired ), -1.f, 1.f ) );
        const float limit = glm::radians( InMaxAngleDegrees );
        if( angle <= limit )
        {
            return Vector3( desired );
        }
        return Vector3( glm::slerp( glm::quat( 1.f, 0.f, 0.f, 0.f ), FromToGlm( aim, desired ), limit / angle ) * aim );
    }
}
