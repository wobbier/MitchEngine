#include "PCH.h"
#include "Net/NetQuantize.h"
#include "Net/BitStream.h"
#include <algorithm>
#include <cmath>

namespace
{
    uint64_t MaxQuantized( int InBits )
    {
        const int bits = std::clamp( InBits, 1, 32 );
        return ( uint64_t( 1 ) << bits ) - 1;
    }

    // The three smaller components of a unit quaternion lie in [-1/sqrt2, 1/sqrt2].
    constexpr float kSmallestThree = 0.70710678f;
}


float NetQuantize::Step( float InMin, float InMax, int InBits )
{
    return ( InMax - InMin ) / static_cast<float>( MaxQuantized( InBits ) );
}


uint32_t NetQuantize::Quantize( float InValue, float InMin, float InMax, int InBits )
{
    if( !( InMax > InMin ) )
    {
        return 0;
    }
    // NaN maps to the minimum rather than to whatever the cast makes of it.
    const float value = std::isnan( InValue ) ? InMin : std::clamp( InValue, InMin, InMax );
    const double t = ( static_cast<double>( value ) - InMin ) / ( static_cast<double>( InMax ) - InMin );
    return static_cast<uint32_t>( std::llround( t * static_cast<double>( MaxQuantized( InBits ) ) ) );
}


float NetQuantize::Dequantize( uint32_t InQuantized, float InMin, float InMax, int InBits )
{
    const uint64_t max = MaxQuantized( InBits );
    const double t = static_cast<double>( std::min<uint64_t>( InQuantized, max ) ) / static_cast<double>( max );
    return static_cast<float>( InMin + t * ( static_cast<double>( InMax ) - InMin ) );
}


void NetQuantize::WriteFloat( BitWriter& InWriter, float InValue, float InMin, float InMax, int InBits )
{
    InWriter.WriteBits( Quantize( InValue, InMin, InMax, InBits ), InBits );
}


float NetQuantize::ReadFloat( BitReader& InReader, float InMin, float InMax, int InBits )
{
    return Dequantize( InReader.ReadBits( InBits ), InMin, InMax, InBits );
}


void NetQuantize::WriteVector3( BitWriter& InWriter, const Vector3& InValue, float InMin, float InMax, int InBits )
{
    WriteFloat( InWriter, InValue.x, InMin, InMax, InBits );
    WriteFloat( InWriter, InValue.y, InMin, InMax, InBits );
    WriteFloat( InWriter, InValue.z, InMin, InMax, InBits );
}


Vector3 NetQuantize::ReadVector3( BitReader& InReader, float InMin, float InMax, int InBits )
{
    const float x = ReadFloat( InReader, InMin, InMax, InBits );
    const float y = ReadFloat( InReader, InMin, InMax, InBits );
    const float z = ReadFloat( InReader, InMin, InMax, InBits );
    return Vector3( x, y, z );
}


void NetQuantize::WriteQuaternion( BitWriter& InWriter, const Quaternion& InValue, int InBits )
{
    float q[4] = { InValue.x, InValue.y, InValue.z, InValue.w };
    const float length = std::sqrt( q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] );
    if( !( length > 1e-12f ) )
    {
        q[0] = q[1] = q[2] = 0.f;
        q[3] = 1.f;
    }
    else
    {
        for( float& c : q )
        {
            c /= length;
        }
    }
    int largest = 0;
    for( int i = 1; i < 4; ++i )
    {
        if( std::abs( q[i] ) > std::abs( q[largest] ) )
        {
            largest = i;
        }
    }
    // The implied component is positive: flip the sign of the whole rotation if it isn't.
    const float sign = q[largest] < 0.f ? -1.f : 1.f;
    InWriter.WriteBits( static_cast<uint32_t>( largest ), 2 );
    for( int i = 0; i < 4; ++i )
    {
        if( i != largest )
        {
            WriteFloat( InWriter, q[i] * sign, -kSmallestThree, kSmallestThree, InBits );
        }
    }
}


Quaternion NetQuantize::ReadQuaternion( BitReader& InReader, int InBits )
{
    const int largest = static_cast<int>( InReader.ReadBits( 2 ) );
    float q[4] = {};
    float sum = 0.f;
    for( int i = 0; i < 4; ++i )
    {
        if( i != largest )
        {
            q[i] = ReadFloat( InReader, -kSmallestThree, kSmallestThree, InBits );
            sum += q[i] * q[i];
        }
    }
    q[largest] = std::sqrt( std::max( 0.f, 1.f - sum ) );
    // Quantization nudges the length; hand back a unit rotation.
    const float length = std::sqrt( sum + q[largest] * q[largest] );
    for( float& c : q )
    {
        c /= length;
    }
    return Quaternion( q[0], q[1], q[2], q[3] );
}
