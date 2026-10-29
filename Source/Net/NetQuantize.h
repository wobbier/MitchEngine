#pragma once
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <cstdint>

class BitReader;
class BitWriter;

// Lossy packing for replicated values (NET-1). A float in [InMin, InMax] at InBits bits comes back
// within Step( ... ) / 2 of what was written (values outside are clamped); rotations use
// "smallest three" (the largest component is implied), so 3 * InBits + 2 bits a quaternion.
namespace NetQuantize
{
    // The spacing between representable values.
    float Step( float InMin, float InMax, int InBits );

    uint32_t Quantize( float InValue, float InMin, float InMax, int InBits );
    float Dequantize( uint32_t InQuantized, float InMin, float InMax, int InBits );

    void WriteFloat( BitWriter& InWriter, float InValue, float InMin, float InMax, int InBits );
    float ReadFloat( BitReader& InReader, float InMin, float InMax, int InBits );

    // Each component in [InMin, InMax].
    void WriteVector3( BitWriter& InWriter, const Vector3& InValue, float InMin, float InMax, int InBits );
    Vector3 ReadVector3( BitReader& InReader, float InMin, float InMax, int InBits );

    // A unit quaternion (normalized on write); q and -q are the same rotation and may come back
    // either way.
    void WriteQuaternion( BitWriter& InWriter, const Quaternion& InValue, int InBits = 12 );
    Quaternion ReadQuaternion( BitReader& InReader, int InBits = 12 );
}
