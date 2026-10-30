#include "PCH.h"
#include "Components/Gameplay/Spline.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_BEGIN( Spline )
    ME_FIELD( Points ).Tooltip( "Local positions the curve passes through, in order" );
    ME_FIELD( Closed ).Tooltip( "Join the last point back to the first" );
ME_REFLECT_END()

namespace
{
    constexpr int kSamplesPerSegment = 16;

    // The four points around segment InSegment (P1 to P2).
    void GetControls( const std::vector<Vector3>& InPoints, bool InClosed, int InSegment, Vector3 OutP[4] )
    {
        const int count = static_cast<int>( InPoints.size() );
        auto at = [&]( int InIndex ) -> Vector3 {
            if( InClosed )
            {
                return InPoints[( ( InIndex % count ) + count ) % count];
            }
            if( InIndex < 0 )
            {
                return InPoints[0] * 2.f - InPoints[1];
            }
            if( InIndex >= count )
            {
                return InPoints[count - 1] * 2.f - InPoints[count - 2];
            }
            return InPoints[InIndex];
        };
        for( int i = 0; i < 4; ++i )
        {
            OutP[i] = at( InSegment - 1 + i );
        }
    }

    // Which segment t falls in and how far along it.
    int Locate( size_t InCount, bool InClosed, float InT, float& OutF )
    {
        const int segments = SplineMath::GetSegmentCount( InCount, InClosed );
        const float scaled = std::clamp( InT, 0.f, 1.f ) * static_cast<float>( segments );
        const int segment = std::min( static_cast<int>( std::floor( scaled ) ), segments - 1 );
        OutF = scaled - static_cast<float>( segment );
        return segment;
    }
}


int SplineMath::GetSegmentCount( size_t InPointCount, bool InClosed )
{
    if( InPointCount < 2 )
    {
        return 0;
    }
    return InClosed ? static_cast<int>( InPointCount ) : static_cast<int>( InPointCount ) - 1;
}


Vector3 SplineMath::Evaluate( const std::vector<Vector3>& InPoints, bool InClosed, float InT )
{
    if( InPoints.empty() )
    {
        return Vector3();
    }
    if( InPoints.size() == 1 )
    {
        return InPoints[0];
    }
    float f = 0.f;
    Vector3 p[4];
    GetControls( InPoints, InClosed, Locate( InPoints.size(), InClosed, InT, f ), p );
    const float f2 = f * f;
    const float f3 = f2 * f;
    return ( p[1] * 2.f + ( p[2] - p[0] ) * f + ( p[0] * 2.f - p[1] * 5.f + p[2] * 4.f - p[3] ) * f2 + ( p[1] * 3.f - p[0] - p[2] * 3.f + p[3] ) * f3 ) * 0.5f;
}


Vector3 SplineMath::Derivative( const std::vector<Vector3>& InPoints, bool InClosed, float InT )
{
    if( InPoints.size() < 2 )
    {
        return Vector3();
    }
    float f = 0.f;
    Vector3 p[4];
    GetControls( InPoints, InClosed, Locate( InPoints.size(), InClosed, InT, f ), p );
    const Vector3 perSegment = ( ( p[2] - p[0] ) + ( p[0] * 2.f - p[1] * 5.f + p[2] * 4.f - p[3] ) * ( 2.f * f ) + ( p[1] * 3.f - p[0] - p[2] * 3.f + p[3] ) * ( 3.f * f * f ) ) * 0.5f;
    return perSegment * static_cast<float>( GetSegmentCount( InPoints.size(), InClosed ) );
}


Spline::Spline()
    : Component( "Spline" )
{
    Points = { Vector3( -3.f, 0.f, 0.f ), Vector3( -1.f, 0.f, 1.f ), Vector3( 1.f, 0.f, -1.f ), Vector3( 3.f, 0.f, 0.f ) };
}


Vector3 Spline::ToWorld( const Vector3& InLocal ) const
{
    if( !Parent || !Parent->HasComponent<Transform>() )
    {
        return InLocal;
    }
    const glm::vec4 world = Parent->GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix() * glm::vec4( InLocal.x, InLocal.y, InLocal.z, 1.f );
    return Vector3( world.x, world.y, world.z );
}


Vector3 Spline::GetLocalPoint( float InT ) const
{
    return SplineMath::Evaluate( Points, Closed, InT );
}


Vector3 Spline::GetPoint( float InT ) const
{
    return ToWorld( GetLocalPoint( InT ) );
}


Vector3 Spline::GetTangent( float InT ) const
{
    // Through the transform as a direction: the difference of two nearby world points.
    const Vector3 local = GetLocalPoint( InT );
    const Vector3 step = SplineMath::Derivative( Points, Closed, InT ) * 0.001f;
    return ( ToWorld( local + step ) - ToWorld( local ) ).Normalized();
}


Vector3 Spline::GetWorldControlPoint( size_t InIndex ) const
{
    return InIndex < Points.size() ? ToWorld( Points[InIndex] ) : Vector3();
}


std::vector<float> Spline::BuildLengthTable() const
{
    const int samples = std::max( SplineMath::GetSegmentCount( Points.size(), Closed ), 1 ) * kSamplesPerSegment;
    std::vector<float> table( static_cast<size_t>( samples ) + 1, 0.f );
    Vector3 previous = GetPoint( 0.f );
    for( int i = 1; i <= samples; ++i )
    {
        const Vector3 point = GetPoint( static_cast<float>( i ) / static_cast<float>( samples ) );
        table[i] = table[i - 1] + ( point - previous ).Length();
        previous = point;
    }
    return table;
}


float Spline::GetLength() const
{
    return BuildLengthTable().back();
}


float Spline::GetTAtDistance( float InDistance ) const
{
    const std::vector<float> table = BuildLengthTable();
    const float length = table.back();
    if( length <= 0.f )
    {
        return 0.f;
    }
    float distance = Closed ? std::fmod( std::fmod( InDistance, length ) + length, length ) : std::clamp( InDistance, 0.f, length );
    const auto upper = std::lower_bound( table.begin(), table.end(), distance );
    if( upper == table.begin() )
    {
        return 0.f;
    }
    if( upper == table.end() )
    {
        return 1.f;
    }
    const size_t index = static_cast<size_t>( upper - table.begin() );
    const float span = table[index] - table[index - 1];
    const float within = span > 0.f ? ( distance - table[index - 1] ) / span : 0.f;
    return ( static_cast<float>( index - 1 ) + within ) / static_cast<float>( table.size() - 1 );
}


float Spline::GetNearestT( const Vector3& InWorld ) const
{
    const int samples = std::max( SplineMath::GetSegmentCount( Points.size(), Closed ), 1 ) * kSamplesPerSegment;
    float best = 0.f;
    float bestDistance = ( GetPoint( 0.f ) - InWorld ).LengthSquared();
    for( int i = 1; i <= samples; ++i )
    {
        const float t = static_cast<float>( i ) / static_cast<float>( samples );
        const float distance = ( GetPoint( t ) - InWorld ).LengthSquared();
        if( distance < bestDistance )
        {
            bestDistance = distance;
            best = t;
        }
    }
    // Refine between the neighbouring samples.
    float low = std::max( best - 1.f / samples, 0.f );
    float high = std::min( best + 1.f / samples, 1.f );
    for( int i = 0; i < 24; ++i )
    {
        const float a = low + ( high - low ) / 3.f;
        const float b = high - ( high - low ) / 3.f;
        if( ( GetPoint( a ) - InWorld ).LengthSquared() < ( GetPoint( b ) - InWorld ).LengthSquared() )
        {
            high = b;
        }
        else
        {
            low = a;
        }
    }
    return ( low + high ) * 0.5f;
}
