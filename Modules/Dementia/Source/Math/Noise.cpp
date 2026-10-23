#include "Noise.h"
#include "Math/Hash.h"
#include <cmath>

namespace
{
    // Sixteen unit directions 22.5 degrees apart (literal constants: identical on every platform).
    constexpr float kGradients2[16][2] = {
        { 1.f, 0.f }, { 0.9238795f, 0.3826834f }, { 0.7071068f, 0.7071068f }, { 0.3826834f, 0.9238795f },
        { 0.f, 1.f }, { -0.3826834f, 0.9238795f }, { -0.7071068f, 0.7071068f }, { -0.9238795f, 0.3826834f },
        { -1.f, 0.f }, { -0.9238795f, -0.3826834f }, { -0.7071068f, -0.7071068f }, { -0.3826834f, -0.9238795f },
        { 0.f, -1.f }, { 0.3826834f, -0.9238795f }, { 0.7071068f, -0.7071068f }, { 0.9238795f, -0.3826834f } };
    // Unit gradients peak at sqrt(1/2) in 2D.
    constexpr float kScale2 = 1.41421356f;
    // Octave-to-octave rotation (a 3-4-5 triangle: exact in binary) and offset.
    constexpr float kRotateCos = 0.8f, kRotateSin = 0.6f;
    constexpr float kOctaveOffsetX = 31.4159f, kOctaveOffsetY = 12.7183f;
    constexpr uint32_t kOctaveSeed = 0x9E3779B9u;

    inline int32_t FloorToInt( float InValue )
    {
        const int32_t truncated = static_cast<int32_t>( InValue );
        return InValue < static_cast<float>( truncated ) ? truncated - 1 : truncated;
    }


    inline int32_t Wrap( int32_t InValue, int32_t InPeriod )
    {
        const int32_t remainder = InValue % InPeriod;
        return remainder < 0 ? remainder + InPeriod : remainder;
    }


    inline float Fade( float t )
    {
        return t * t * t * ( t * ( t * 6.f - 15.f ) + 10.f );
    }


    inline float FadeDerivative( float t )
    {
        return 30.f * t * t * ( t * ( t - 2.f ) + 1.f );
    }


    template<typename CornerHash>
    Noise::Sample2 GradientCore( float InX, float InY, CornerHash&& InHash )
    {
        const int32_t ix = FloorToInt( InX ), iy = FloorToInt( InY );
        const float fx = InX - static_cast<float>( ix ), fy = InY - static_cast<float>( iy );
        const float ux = Fade( fx ), uy = Fade( fy );
        const float dux = FadeDerivative( fx ), duy = FadeDerivative( fy );
        const float* ga = kGradients2[InHash( ix, iy ) >> 28];
        const float* gb = kGradients2[InHash( ix + 1, iy ) >> 28];
        const float* gc = kGradients2[InHash( ix, iy + 1 ) >> 28];
        const float* gd = kGradients2[InHash( ix + 1, iy + 1 ) >> 28];
        const float va = ga[0] * fx + ga[1] * fy;
        const float vb = gb[0] * ( fx - 1.f ) + gb[1] * fy;
        const float vc = gc[0] * fx + gc[1] * ( fy - 1.f );
        const float vd = gd[0] * ( fx - 1.f ) + gd[1] * ( fy - 1.f );
        const float k = va - vb - vc + vd;
        Noise::Sample2 out;
        out.Value = ( va + ux * ( vb - va ) + uy * ( vc - va ) + ux * uy * k ) * kScale2;
        out.Dx = ( ga[0] + ux * ( gb[0] - ga[0] ) + uy * ( gc[0] - ga[0] ) + ux * uy * ( ga[0] - gb[0] - gc[0] + gd[0] ) + dux * ( uy * k + vb - va ) ) * kScale2;
        out.Dy = ( ga[1] + ux * ( gb[1] - ga[1] ) + uy * ( gc[1] - ga[1] ) + ux * uy * ( ga[1] - gb[1] - gc[1] + gd[1] ) + duy * ( ux * k + vc - va ) ) * kScale2;
        return out;
    }


    inline float Gradient3( uint32_t InHash, float x, float y, float z )
    {
        // Perlin's improved-noise cube edge directions.
        const uint32_t h = InHash >> 28;
        const float u = h < 8 ? x : y;
        const float v = h < 4 ? y : ( h == 12 || h == 14 ? x : z );
        return ( ( h & 1 ) ? -u : u ) + ( ( h & 2 ) ? -v : v );
    }


    inline float Lerp( float a, float b, float t )
    {
        return a + ( b - a ) * t;
    }


    // Advances an octave: p' = L * R * p + offset; M tracks d(p')/d(p0) for input-space gradients.
    struct OctaveFrame
    {
        float X, Y;
        float M00 = 1.f, M01 = 0.f, M10 = 0.f, M11 = 1.f;

        void Next( float InLacunarity )
        {
            const float x = InLacunarity * ( kRotateCos * X - kRotateSin * Y ) + kOctaveOffsetX;
            const float y = InLacunarity * ( kRotateSin * X + kRotateCos * Y ) + kOctaveOffsetY;
            X = x; Y = y;
            const float m00 = InLacunarity * ( kRotateCos * M00 - kRotateSin * M10 );
            const float m01 = InLacunarity * ( kRotateCos * M01 - kRotateSin * M11 );
            const float m10 = InLacunarity * ( kRotateSin * M00 + kRotateCos * M10 );
            const float m11 = InLacunarity * ( kRotateSin * M01 + kRotateCos * M11 );
            M00 = m00; M01 = m01; M10 = m10; M11 = m11;
        }

        // The octave's gradient carried back to input space (M^T * g).
        void ToInput( const Noise::Sample2& InSample, float& OutDx, float& OutDy ) const
        {
            OutDx = M00 * InSample.Dx + M10 * InSample.Dy;
            OutDy = M01 * InSample.Dx + M11 * InSample.Dy;
        }
    };
}

namespace Noise
{
    Sample2 Gradient2( float InX, float InY, uint32_t InSeed )
    {
        return GradientCore( InX, InY, [InSeed]( int32_t x, int32_t y ) { return Hash::Lattice( x, y, InSeed ); } );
    }


    float Value2( float InX, float InY, uint32_t InSeed )
    {
        return Gradient2( InX, InY, InSeed ).Value;
    }


    Sample2 GradientPeriodic2( float InX, float InY, int32_t InPeriodX, int32_t InPeriodY, uint32_t InSeed )
    {
        const int32_t px = InPeriodX > 0 ? InPeriodX : 1, py = InPeriodY > 0 ? InPeriodY : 1;
        return GradientCore( InX, InY, [=]( int32_t x, int32_t y ) { return Hash::Lattice( Wrap( x, px ), Wrap( y, py ), InSeed ); } );
    }


    float Value3( float InX, float InY, float InZ, uint32_t InSeed )
    {
        const int32_t ix = FloorToInt( InX ), iy = FloorToInt( InY ), iz = FloorToInt( InZ );
        const float fx = InX - static_cast<float>( ix ), fy = InY - static_cast<float>( iy ), fz = InZ - static_cast<float>( iz );
        const float u = Fade( fx ), v = Fade( fy ), w = Fade( fz );
        auto corner = [&]( int32_t dx, int32_t dy, int32_t dz ) {
            return Gradient3( Hash::Lattice( ix + dx, iy + dy, iz + dz, InSeed ), fx - static_cast<float>( dx ), fy - static_cast<float>( dy ), fz - static_cast<float>( dz ) );
        };
        const float x00 = Lerp( corner( 0, 0, 0 ), corner( 1, 0, 0 ), u );
        const float x10 = Lerp( corner( 0, 1, 0 ), corner( 1, 1, 0 ), u );
        const float x01 = Lerp( corner( 0, 0, 1 ), corner( 1, 0, 1 ), u );
        const float x11 = Lerp( corner( 0, 1, 1 ), corner( 1, 1, 1 ), u );
        return Lerp( Lerp( x00, x10, v ), Lerp( x01, x11, v ), w );
    }


    float Fbm2( float InX, float InY, uint32_t InSeed, const Fractal& InFractal, float* OutDx, float* OutDy )
    {
        OctaveFrame frame{ InX, InY };
        float sum = 0.f, amplitude = 1.f, norm = 0.f, dx = 0.f, dy = 0.f;
        for( int i = 0; i < InFractal.Octaves; ++i )
        {
            const Sample2 sample = Gradient2( frame.X, frame.Y, InSeed + kOctaveSeed * static_cast<uint32_t>( i ) );
            float gx = 0.f, gy = 0.f;
            frame.ToInput( sample, gx, gy );
            sum += amplitude * sample.Value;
            dx += amplitude * gx;
            dy += amplitude * gy;
            norm += amplitude;
            amplitude *= InFractal.Gain;
            frame.Next( InFractal.Lacunarity );
        }
        const float scale = norm > 0.f ? 1.f / norm : 0.f;
        if( OutDx ) *OutDx = dx * scale;
        if( OutDy ) *OutDy = dy * scale;
        return sum * scale;
    }


    float FbmPeriodic2( float InX, float InY, int32_t InPeriodX, int32_t InPeriodY, uint32_t InSeed, const Fractal& InFractal )
    {
        const int32_t lacunarity = InFractal.Lacunarity >= 2.f ? static_cast<int32_t>( InFractal.Lacunarity ) : 2;
        float sum = 0.f, amplitude = 1.f, norm = 0.f, frequency = 1.f;
        int32_t px = InPeriodX, py = InPeriodY;
        for( int i = 0; i < InFractal.Octaves; ++i )
        {
            sum += amplitude * GradientPeriodic2( InX * frequency, InY * frequency, px, py, InSeed + kOctaveSeed * static_cast<uint32_t>( i ) ).Value;
            norm += amplitude;
            amplitude *= InFractal.Gain;
            frequency *= static_cast<float>( lacunarity );
            px *= lacunarity;
            py *= lacunarity;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }


    float Fbm3( float InX, float InY, float InZ, uint32_t InSeed, const Fractal& InFractal )
    {
        float sum = 0.f, amplitude = 1.f, norm = 0.f, x = InX, y = InY, z = InZ;
        for( int i = 0; i < InFractal.Octaves; ++i )
        {
            sum += amplitude * Value3( x, y, z, InSeed + kOctaveSeed * static_cast<uint32_t>( i ) );
            norm += amplitude;
            amplitude *= InFractal.Gain;
            x = x * InFractal.Lacunarity + kOctaveOffsetX;
            y = y * InFractal.Lacunarity + kOctaveOffsetY;
            z = z * InFractal.Lacunarity - kOctaveOffsetX;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }


    float Ridged2( float InX, float InY, uint32_t InSeed, const Fractal& InFractal, float InOffset, float InRidgeGain )
    {
        OctaveFrame frame{ InX, InY };
        float sum = 0.f, amplitude = 1.f, norm = 0.f, weight = 1.f;
        for( int i = 0; i < InFractal.Octaves; ++i )
        {
            float signal = InOffset - std::fabs( Gradient2( frame.X, frame.Y, InSeed + kOctaveSeed * static_cast<uint32_t>( i ) ).Value );
            signal *= signal * weight;
            weight = signal * InRidgeGain;
            weight = weight < 0.f ? 0.f : weight > 1.f ? 1.f : weight;
            sum += signal * amplitude;
            norm += amplitude * InOffset * InOffset;
            amplitude *= InFractal.Gain;
            frame.Next( InFractal.Lacunarity );
        }
        return norm > 0.f ? sum / norm : 0.f;
    }


    float HybridMultifractal2( float InX, float InY, uint32_t InSeed, int InOctaves, float InLacunarity, float InH, float InOffset )
    {
        OctaveFrame frame{ InX, InY };
        const float step = std::pow( InLacunarity, -InH );
        float amplitude = 1.f;
        float result = ( Gradient2( frame.X, frame.Y, InSeed ).Value + InOffset ) * amplitude;
        float weight = result;
        for( int i = 1; i < InOctaves; ++i )
        {
            frame.Next( InLacunarity );
            amplitude *= step;
            weight = weight > 1.f ? 1.f : weight < 0.f ? 0.f : weight;
            const float signal = ( Gradient2( frame.X, frame.Y, InSeed + kOctaveSeed * static_cast<uint32_t>( i ) ).Value + InOffset ) * amplitude;
            result += weight * signal;
            weight *= signal;
        }
        return result;
    }


    float ErodedFbm2( float InX, float InY, uint32_t InSeed, const Fractal& InFractal, float InDamping )
    {
        OctaveFrame frame{ InX, InY };
        float sum = 0.f, amplitude = 1.f, norm = 0.f, dx = 0.f, dy = 0.f;
        for( int i = 0; i < InFractal.Octaves; ++i )
        {
            const Sample2 sample = Gradient2( frame.X, frame.Y, InSeed + kOctaveSeed * static_cast<uint32_t>( i ) );
            float gx = 0.f, gy = 0.f;
            frame.ToInput( sample, gx, gy );
            dx += amplitude * gx;
            dy += amplitude * gy;
            sum += amplitude * sample.Value / ( 1.f + InDamping * ( dx * dx + dy * dy ) );
            norm += amplitude;
            amplitude *= InFractal.Gain;
            frame.Next( InFractal.Lacunarity );
        }
        return norm > 0.f ? sum / norm : 0.f;
    }


    Cell Voronoi2( float InX, float InY, uint32_t InSeed, float InJitter, int32_t InPeriodX, int32_t InPeriodY )
    {
        const int32_t ix = FloorToInt( InX ), iy = FloorToInt( InY );
        Cell out;
        float best1 = 1e30f, best2 = 1e30f;
        for( int32_t dy = -1; dy <= 1; ++dy )
        {
            for( int32_t dx = -1; dx <= 1; ++dx )
            {
                const int32_t cx = ix + dx, cy = iy + dy;
                const int32_t hx = InPeriodX > 0 ? Wrap( cx, InPeriodX ) : cx;
                const int32_t hy = InPeriodY > 0 ? Wrap( cy, InPeriodY ) : cy;
                const uint32_t h = Hash::Lattice( hx, hy, InSeed );
                const float fx = static_cast<float>( cx ) + 0.5f + InJitter * ( Hash::ToUnit( h ) - 0.5f );
                const float fy = static_cast<float>( cy ) + 0.5f + InJitter * ( Hash::ToUnit( Hash::Mix32( h ) ) - 0.5f );
                const float distance = ( fx - InX ) * ( fx - InX ) + ( fy - InY ) * ( fy - InY );
                if( distance < best1 )
                {
                    best2 = best1;
                    best1 = distance;
                    out.X = fx;
                    out.Y = fy;
                    out.Id = h;
                }
                else if( distance < best2 )
                {
                    best2 = distance;
                }
            }
        }
        out.F1 = std::sqrt( best1 );
        out.F2 = std::sqrt( best2 );
        return out;
    }
}
