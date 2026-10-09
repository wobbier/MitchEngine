#include "AnimationClip.h"
#include <algorithm>
#include <glm/gtc/quaternion.hpp>

namespace Moonlight
{
    namespace
    {
        // The key pair around InTime and the blend between them.
        bool FindSpan( const std::vector<float>& InTimes, float InTime, size_t& OutA, size_t& OutB, float& OutT )
        {
            if( InTimes.empty() )
            {
                return false;
            }
            if( InTimes.size() == 1 || InTime <= InTimes.front() )
            {
                OutA = OutB = 0;
                OutT = 0.f;
                return true;
            }
            if( InTime >= InTimes.back() )
            {
                OutA = OutB = InTimes.size() - 1;
                OutT = 0.f;
                return true;
            }
            const auto upper = std::upper_bound( InTimes.begin(), InTimes.end(), InTime );
            OutB = static_cast<size_t>( upper - InTimes.begin() );
            OutA = OutB - 1;
            const float span = InTimes[OutB] - InTimes[OutA];
            OutT = span > 0.f ? ( InTime - InTimes[OutA] ) / span : 0.f;
            return true;
        }
    }


    void AnimationClip::Sample( const AnimationChannel& InChannel, float InTime, Vector3& InOutPosition, Quaternion& InOutRotation, Vector3& InOutScale )
    {
        size_t a = 0;
        size_t b = 0;
        float t = 0.f;
        if( FindSpan( InChannel.PositionTimes, InTime, a, b, t ) && b < InChannel.Positions.size() )
        {
            InOutPosition = InChannel.Positions[a] + ( InChannel.Positions[b] - InChannel.Positions[a] ) * t;
        }
        if( FindSpan( InChannel.RotationTimes, InTime, a, b, t ) && b < InChannel.Rotations.size() )
        {
            const Quaternion& qa = InChannel.Rotations[a];
            const Quaternion& qb = InChannel.Rotations[b];
            const glm::quat result = glm::slerp( glm::quat( qa.w, qa.x, qa.y, qa.z ), glm::quat( qb.w, qb.x, qb.y, qb.z ), t );
            InOutRotation = Quaternion( result.x, result.y, result.z, result.w );
        }
        if( FindSpan( InChannel.ScaleTimes, InTime, a, b, t ) && b < InChannel.Scales.size() )
        {
            InOutScale = InChannel.Scales[a] + ( InChannel.Scales[b] - InChannel.Scales[a] ) * t;
        }
    }


    int AnimationClip::FindChannel( const std::string& InNodeName ) const
    {
        for( size_t i = 0; i < Channels.size(); ++i )
        {
            if( Channels[i].NodeName == InNodeName )
            {
                return static_cast<int>( i );
            }
        }
        return -1;
    }
}
