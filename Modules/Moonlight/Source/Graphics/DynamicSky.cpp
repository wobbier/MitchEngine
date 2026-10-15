#include "DynamicSky.h"
#include "Camera/CameraData.h"
#include "Device/FrameBuffer.h"
#include "Primitives/Cube.h"
#include "RenderViews.h"
#include "Utils/BGFXUtils.h"
#include "optick.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace Moonlight
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        constexpr float kDegrees = kPi / 180.f;
        constexpr float kBottomRadius = 6360.f;    // km, match Atmosphere.sh
        constexpr float kTopRadius = 6460.f;

        constexpr uint16_t kTransmittanceWidth = 256;
        constexpr uint16_t kTransmittanceHeight = 64;
        constexpr uint16_t kMultiScatterSize = 32;
        constexpr uint16_t kSkyViewWidth = 256;
        constexpr uint16_t kSkyViewHeight = 128;

        constexpr int kShapeSize = 128;
        constexpr int kDetailSize = 32;
        constexpr int kWeatherSize = 256;
        constexpr float kWeatherTile = 60.f;        // km, match Clouds.sh
        constexpr float kShapeTile = 9.f;

        // Real angular radii (radians); the settings scale them.
        constexpr float kSunRadius = 0.004654f;
        constexpr float kMoonRadius = 0.004520f;
        // Sunlight outside the atmosphere in engine units: about 3 at noon on the ground, what the
        // scene lighting is balanced for.
        constexpr float kSunIlluminance = 3.4f;
        // Sun disc luminance per unit illuminance (a real disc is ~10^4 times brighter still).
        constexpr float kSunDiscLuminance = 12.f;
        // A full moon's light relative to the sun's, and its disc luminance. Art directed: real
        // moonlight is ~1/400000 of sunlight, which no fixed exposure could show next to daylight.
        constexpr float kMoonIlluminance = 0.06f;
        constexpr float kMoonDiscLuminance = 1.6f;
        // Scene moonlight is lifted for a fixed exposure. Scattering keeps a much lower level so
        // that the moon doesn't turn surrounding clouds and haze into a second daylight sky.
        constexpr float kMoonScattering = 0.06f;
        constexpr float kMoonTint[3] = { 0.62f, 0.78f, 1.f };
        // Art scales on the light the air and the clouds scatter. A clean-air model (and the octave
        // approximation of the clouds' multiple scattering) comes out darker than real skies next to
        // sunlit ground: real air carries more aerosols and clouds scatter many more times.
        constexpr float kSkyBrightness = 2.5f;
        constexpr float kCloudBrightness = 2.f;
        // The sun's light is lifted as it sets, standing in for the eye adapting at dusk: at a fixed
        // exposure the twilight sky would be black within minutes of sunset.
        constexpr float kTwilightLift = 6.f;
        constexpr float kGroundAlbedo = 0.3f;
        // Night sky levels sit just above the tonemapper's toe (ACES clips below ~0.0035).
        constexpr float kNightGlow = 0.005f;
        constexpr float kMilkyWayBrightness = 0.1f;

        constexpr float kCloudExtinction = 45.f;   // per km at full density
        constexpr float kCloudEvolution = 0.0015f; // km of shape drift per second
        constexpr float kCloudAmbient = 1.f;
        constexpr float kCloudSteps = 48.f;
        constexpr float kCloudLightSteps = 6.f;
        constexpr float kCloudCaptureSteps = 24.f;
        constexpr float kCloudCaptureLightSteps = 4.f;
        constexpr float kCloudMaxDistance = 80.f;  // km marched at most
        constexpr float kCloudFadeDistance = 50.f; // km over which far clouds dissolve into the haze
        constexpr float kCloudHistoryWeight = 0.1f;

        constexpr uint64_t kClamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

        float Smoothstep( float InEdge0, float InEdge1, float InValue )
        {
            const float t = std::clamp( ( InValue - InEdge0 ) / ( InEdge1 - InEdge0 ), 0.f, 1.f );
            return t * t * ( 3.f - 2.f * t );
        }

        float Luminance( const bx::Vec3& InColor )
        {
            return InColor.x * 0.2126f + InColor.y * 0.7152f + InColor.z * 0.0722f;
        }

        bx::Vec3 Scale( const bx::Vec3& InA, const bx::Vec3& InB )
        {
            return { InA.x * InB.x, InA.y * InB.y, InA.z * InB.z };
        }

        uint64_t Mix( uint64_t InHash, uint64_t InValue )
        {
            return InHash ^ ( InValue + 0x9e3779b97f4a7c15ULL + ( InHash << 6 ) + ( InHash >> 2 ) );
        }

        uint64_t Quantize( float InValue, float InSteps )
        {
            return static_cast<uint64_t>( static_cast<int64_t>( std::lround( InValue * InSteps ) ) );
        }

        void DestroyIfValid( bgfx::TextureHandle& InHandle )
        {
            if( bgfx::isValid( InHandle ) )
            {
                bgfx::destroy( InHandle );
            }
            InHandle = BGFX_INVALID_HANDLE;
        }

        void DestroyIfValid( bgfx::FrameBufferHandle& InHandle )
        {
            if( bgfx::isValid( InHandle ) )
            {
                bgfx::destroy( InHandle );
            }
            InHandle = BGFX_INVALID_HANDLE;
        }

        void DestroyIfValid( bgfx::UniformHandle& InHandle )
        {
            if( bgfx::isValid( InHandle ) )
            {
                bgfx::destroy( InHandle );
            }
            InHandle = BGFX_INVALID_HANDLE;
        }

        void SetupFullscreenView( bgfx::ViewId InView, bgfx::FrameBufferHandle InTarget, uint16_t InWidth, uint16_t InHeight )
        {
            float ortho[16];
            bx::mtxOrtho( ortho, 0.f, 1.f, 1.f, 0.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
            bgfx::setViewFrameBuffer( InView, InTarget );
            bgfx::setViewRect( InView, 0, 0, InWidth, InHeight );
            bgfx::setViewTransform( InView, nullptr, ortho );
            bgfx::touch( InView );
        }

        void SubmitFullscreen( bgfx::ViewId InView, const ShaderCommand& InProgram )
        {
            bgfx::setState( BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A );
            screenSpaceQuad( 1.f, 1.f, 0.f, bgfx::getCaps()->originBottomLeft );
            bgfx::submit( InView, InProgram.GetProgram() );
        }

        // --- Atmosphere on the CPU (the light the sun and moon cast on the scene) ---------------

        void AtmosphereExtinction( float InAltitude, float InAerosol, float OutExtinction[3] )
        {
            const float h = std::max( InAltitude, 0.f );
            const float rayleigh = std::exp( -h / 8.f );
            const float mie = std::exp( -h / 1.2f ) * InAerosol;
            const float ozone = std::max( 0.f, 1.f - std::abs( h - 25.f ) / 15.f );
            OutExtinction[0] = 5.802e-3f * rayleigh + 4.44e-3f * mie + 0.650e-3f * ozone;
            OutExtinction[1] = 13.558e-3f * rayleigh + 4.44e-3f * mie + 1.881e-3f * ozone;
            OutExtinction[2] = 33.1e-3f * rayleigh + 4.44e-3f * mie + 0.085e-3f * ozone;
        }

        // --- Celestial positions ---------------------------------------------------------------

        bx::Vec3 Equatorial( float InRightAscension, float InDeclination )
        {
            return { std::cos( InDeclination ) * std::cos( InRightAscension ), std::cos( InDeclination ) * std::sin( InRightAscension ), std::sin( InDeclination ) };
        }

        // A point on the ecliptic (latitude 0) to right ascension and declination.
        void EclipticToEquatorial( float InLongitude, float& OutRightAscension, float& OutDeclination )
        {
            constexpr float kObliquity = 23.44f * kDegrees;
            OutRightAscension = std::atan2( std::cos( kObliquity ) * std::sin( InLongitude ), std::cos( InLongitude ) );
            OutDeclination = std::asin( std::sin( kObliquity ) * std::sin( InLongitude ) );
        }

        // --- Cloud noise ---------------------------------------------------------------------

        uint32_t Hash( uint32_t x )
        {
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        }

        uint32_t Hash( int x, int y, int z, uint32_t InSeed )
        {
            return Hash( static_cast<uint32_t>( x ) * 73856093U ^ Hash( static_cast<uint32_t>( y ) * 19349663U ^ Hash( static_cast<uint32_t>( z ) * 83492791U ^ InSeed ) ) );
        }

        float HashFloat( int x, int y, int z, uint32_t InSeed )
        {
            return static_cast<float>( Hash( x, y, z, InSeed ) >> 8 ) * ( 1.f / 16777216.f );
        }

        int Wrap( int InValue, int InPeriod )
        {
            return ( ( InValue % InPeriod ) + InPeriod ) % InPeriod;
        }

        float Remap( float InValue, float InLow, float InHigh )
        {
            return ( InValue - InLow ) / std::max( InHigh - InLow, 1e-4f );
        }

        // Tiling Worley (cellular) noise: one feature point per cell of a period^3 grid; 1 on a point,
        // 0 a cell or more away. Coordinates are 0..1 per tile.
        class Worley
        {
        public:
            Worley( int InPeriod, uint32_t InSeed )
                : m_period( InPeriod )
                , m_points( static_cast<size_t>( InPeriod ) * InPeriod * InPeriod * 3 )
            {
                for( int z = 0; z < InPeriod; ++z )
                {
                    for( int y = 0; y < InPeriod; ++y )
                    {
                        for( int x = 0; x < InPeriod; ++x )
                        {
                            float* point = &m_points[( ( static_cast<size_t>( z ) * InPeriod + y ) * InPeriod + x ) * 3];
                            point[0] = HashFloat( x, y, z, InSeed );
                            point[1] = HashFloat( x, y, z, InSeed + 1 );
                            point[2] = HashFloat( x, y, z, InSeed + 2 );
                        }
                    }
                }
            }

            float Sample( float u, float v, float w ) const
            {
                const float px = u * m_period;
                const float py = v * m_period;
                const float pz = w * m_period;
                const int cx = static_cast<int>( std::floor( px ) );
                const int cy = static_cast<int>( std::floor( py ) );
                const int cz = static_cast<int>( std::floor( pz ) );
                float nearest = 1e9f;
                for( int dz = -1; dz <= 1; ++dz )
                {
                    for( int dy = -1; dy <= 1; ++dy )
                    {
                        for( int dx = -1; dx <= 1; ++dx )
                        {
                            const int x = cx + dx;
                            const int y = cy + dy;
                            const int z = cz + dz;
                            const float* point = &m_points[( ( static_cast<size_t>( Wrap( z, m_period ) ) * m_period + Wrap( y, m_period ) ) * m_period + Wrap( x, m_period ) ) * 3];
                            const float ex = static_cast<float>( x ) + point[0] - px;
                            const float ey = static_cast<float>( y ) + point[1] - py;
                            const float ez = static_cast<float>( z ) + point[2] - pz;
                            nearest = std::min( nearest, ex * ex + ey * ey + ez * ez );
                        }
                    }
                }
                return 1.f - std::min( std::sqrt( nearest ), 1.f );
            }

        private:
            int m_period;
            std::vector<float> m_points;
        };

        float Fade( float t )
        {
            return t * t * t * ( t * ( t * 6.f - 15.f ) + 10.f );
        }

        float Gradient( uint32_t InHash, float x, float y, float z )
        {
            switch( InHash & 15 )
            {
            case 0: return x + y;
            case 1: return -x + y;
            case 2: return x - y;
            case 3: return -x - y;
            case 4: return x + z;
            case 5: return -x + z;
            case 6: return x - z;
            case 7: return -x - z;
            case 8: return y + z;
            case 9: return -y + z;
            case 10: return y - z;
            case 11: return -y - z;
            case 12: return x + y;
            case 13: return -x + y;
            case 14: return -y + z;
            default: return -y - z;
            }
        }

        // Tiling gradient (Perlin) noise, about -1..1, InPeriod lattice cells per tile.
        float Perlin( float u, float v, float w, int InPeriod, uint32_t InSeed )
        {
            const float px = u * InPeriod;
            const float py = v * InPeriod;
            const float pz = w * InPeriod;
            const int x0 = static_cast<int>( std::floor( px ) );
            const int y0 = static_cast<int>( std::floor( py ) );
            const int z0 = static_cast<int>( std::floor( pz ) );
            const float fx = px - static_cast<float>( x0 );
            const float fy = py - static_cast<float>( y0 );
            const float fz = pz - static_cast<float>( z0 );
            float corners[8];
            for( int i = 0; i < 8; ++i )
            {
                const int dx = i & 1;
                const int dy = ( i >> 1 ) & 1;
                const int dz = i >> 2;
                const uint32_t hash = Hash( Wrap( x0 + dx, InPeriod ), Wrap( y0 + dy, InPeriod ), Wrap( z0 + dz, InPeriod ), InSeed );
                corners[i] = Gradient( hash, fx - static_cast<float>( dx ), fy - static_cast<float>( dy ), fz - static_cast<float>( dz ) );
            }
            const float sx = Fade( fx );
            const float sy = Fade( fy );
            const float sz = Fade( fz );
            const float x00 = corners[0] + sx * ( corners[1] - corners[0] );
            const float x10 = corners[2] + sx * ( corners[3] - corners[2] );
            const float x01 = corners[4] + sx * ( corners[5] - corners[4] );
            const float x11 = corners[6] + sx * ( corners[7] - corners[6] );
            const float y0v = x00 + sy * ( x10 - x00 );
            const float y1v = x01 + sy * ( x11 - x01 );
            return y0v + sz * ( y1v - y0v );
        }

        float PerlinFbm( float u, float v, float w, int InPeriod, int InOctaves, uint32_t InSeed )
        {
            float sum = 0.f;
            float amplitude = 1.f;
            float total = 0.f;
            for( int octave = 0; octave < InOctaves; ++octave )
            {
                sum += Perlin( u, v, w, InPeriod << octave, InSeed + static_cast<uint32_t>( octave ) ) * amplitude;
                total += amplitude;
                amplitude *= 0.5f;
            }
            return sum / total;
        }

        // Remaps values so their histogram is flat: thresholding at t then keeps a fraction 1 - t,
        // which is what the cloud coverage relies on.
        void Equalize( std::vector<float>& InOutValues )
        {
            if( InOutValues.empty() )
            {
                return;
            }
            const auto range = std::minmax_element( InOutValues.begin(), InOutValues.end() );
            const float low = *range.first;
            const float scale = 1.f / std::max( *range.second - low, 1e-6f );
            constexpr int kBins = 4096;
            auto bin = [&]( float InValue ) { return std::clamp( static_cast<int>( ( InValue - low ) * scale * kBins ), 0, kBins - 1 ); };
            std::vector<uint32_t> histogram( kBins, 0 );
            for( float value : InOutValues )
            {
                ++histogram[bin( value )];
            }
            std::vector<float> cumulative( kBins );
            uint64_t running = 0;
            const double count = static_cast<double>( InOutValues.size() );
            for( int i = 0; i < kBins; ++i )
            {
                cumulative[i] = static_cast<float>( ( static_cast<double>( running ) + histogram[i] * 0.5 ) / count );
                running += histogram[i];
            }
            for( float& value : InOutValues )
            {
                value = cumulative[bin( value )];
            }
        }

        uint8_t ToByte( float InValue )
        {
            return static_cast<uint8_t>( std::clamp( InValue, 0.f, 1.f ) * 255.f + 0.5f );
        }

        template<typename Fn>
        void ParallelFor( int InCount, const Fn& InFn )
        {
            const int threads = std::clamp( static_cast<int>( std::thread::hardware_concurrency() ), 1, 32 );
            std::atomic<int> next{ 0 };
            auto work = [&]() {
                for( int i = next++; i < InCount; i = next++ )
                {
                    InFn( i );
                }
            };
            std::vector<std::thread> pool;
            for( int t = 1; t < threads; ++t )
            {
                pool.emplace_back( work );
            }
            work();
            for( std::thread& thread : pool )
            {
                thread.join();
            }
        }

        // Trilinear/bilinear lookups matching the GPU's linear filtering with wrapping.
        float SampleVolume( const std::vector<uint8_t>& InData, int InSize, float u, float v, float w )
        {
            const float x = u * InSize - 0.5f;
            const float y = v * InSize - 0.5f;
            const float z = w * InSize - 0.5f;
            const int x0 = static_cast<int>( std::floor( x ) );
            const int y0 = static_cast<int>( std::floor( y ) );
            const int z0 = static_cast<int>( std::floor( z ) );
            const float fx = x - static_cast<float>( x0 );
            const float fy = y - static_cast<float>( y0 );
            const float fz = z - static_cast<float>( z0 );
            auto at = [&]( int i, int j, int k ) {
                return static_cast<float>( InData[( static_cast<size_t>( Wrap( k, InSize ) ) * InSize + Wrap( j, InSize ) ) * InSize + Wrap( i, InSize )] ) / 255.f;
            };
            auto lerp = []( float a, float b, float t ) { return a + ( b - a ) * t; };
            const float c00 = lerp( at( x0, y0, z0 ), at( x0 + 1, y0, z0 ), fx );
            const float c10 = lerp( at( x0, y0 + 1, z0 ), at( x0 + 1, y0 + 1, z0 ), fx );
            const float c01 = lerp( at( x0, y0, z0 + 1 ), at( x0 + 1, y0, z0 + 1 ), fx );
            const float c11 = lerp( at( x0, y0 + 1, z0 + 1 ), at( x0 + 1, y0 + 1, z0 + 1 ), fx );
            return lerp( lerp( c00, c10, fy ), lerp( c01, c11, fy ), fz );
        }

        float SampleWeather( const std::vector<uint8_t>& InData, float u, float v, int InChannel )
        {
            const float x = u * kWeatherSize - 0.5f;
            const float y = v * kWeatherSize - 0.5f;
            const int x0 = static_cast<int>( std::floor( x ) );
            const int y0 = static_cast<int>( std::floor( y ) );
            const float fx = x - static_cast<float>( x0 );
            const float fy = y - static_cast<float>( y0 );
            auto at = [&]( int i, int j ) {
                return static_cast<float>( InData[( static_cast<size_t>( Wrap( j, kWeatherSize ) ) * kWeatherSize + Wrap( i, kWeatherSize ) ) * 2 + InChannel] ) / 255.f;
            };
            const float top = at( x0, y0 ) + ( at( x0 + 1, y0 ) - at( x0, y0 ) ) * fx;
            const float bottom = at( x0, y0 + 1 ) + ( at( x0 + 1, y0 + 1 ) - at( x0, y0 + 1 ) ) * fx;
            return top + ( bottom - top ) * fy;
        }
    }


    // The air in the way and the planet below the horizon, as in Atmosphere.sh.
    float ComputeDiscVisibility( float InSunRadius, float InMoonRadius, float InSeparation )
    {
        const double s = std::max<double>( InSunRadius, 1e-8 );
        const double m = std::max<double>( InMoonRadius, 0.0 );
        const double d = std::max<double>( InSeparation, 0.0 );
        if( d >= s + m ) return 1.f;
        if( d <= std::abs( s - m ) ) return static_cast<float>( std::max( 0.0, 1.0 - m * m / ( s * s ) ) );
        const double a = std::acos( std::clamp( ( d * d + s * s - m * m ) / ( 2.0 * d * s ), -1.0, 1.0 ) );
        const double b = std::acos( std::clamp( ( d * d + m * m - s * s ) / ( 2.0 * d * m ), -1.0, 1.0 ) );
        const double lens = std::sqrt( std::max( ( -d + s + m ) * ( d + s - m ) * ( d - s + m ) * ( d + s + m ), 0.0 ) );
        return static_cast<float>( std::clamp( 1.0 - ( s * s * a + m * m * b - 0.5 * lens ) / ( 3.141592653589793 * s * s ), 0.0, 1.0 ) );
    }

    float SolarEclipse::Visibility( const bx::Vec3& InWorldPosition ) const
    {
        const bx::Vec3 moon = bx::normalize( bx::sub( bx::mul( Moon, MoonDistance ), InWorldPosition ) );
        const float separation = std::atan2( bx::length( bx::cross( Sun, moon ) ), bx::dot( Sun, moon ) );
        const float radius = MoonRadius * MoonDistance / std::max( bx::length( bx::sub( bx::mul( Moon, MoonDistance ), InWorldPosition ) ), 1.f );
        return ComputeDiscVisibility( SunRadius, radius, separation );
    }

    void SolarEclipse::Uniforms( float OutSun[4], float OutMoon[4], float OutParams[4] ) const
    {
        const float sun[4] = { Sun.x, Sun.y, Sun.z, SunRadius };
        const float moon[4] = { Moon.x, Moon.y, Moon.z, MoonRadius };
        const float params[4] = { 1.f / MoonDistance, 1.f, 0.f, 0.f };
        std::copy( sun, sun + 4, OutSun );
        std::copy( moon, moon + 4, OutMoon );
        std::copy( params, params + 4, OutParams );
    }

    bx::Vec3 ComputeSkyTransmittance( float InAltitudeKm, float InMu, float InAerosol, float InRadius )
    {
        const float r = kBottomRadius + InAltitudeKm;
        const float sinHorizon = kBottomRadius / r;
        const float cosHorizon = -std::sqrt( std::max( 1.f - sinHorizon * sinHorizon, 0.f ) );
        const float radius = std::max( InRadius, 1e-4f );
        const float visible = Smoothstep( -radius, radius, InMu - cosHorizon );
        if( visible <= 0.f )
        {
            return { 0.f, 0.f, 0.f };
        }
        const float mu = std::max( InMu, cosHorizon );
        const float distance = -r * mu + std::sqrt( std::max( r * r * ( mu * mu - 1.f ) + kTopRadius * kTopRadius, 0.f ) );
        constexpr int kSteps = 40;
        const float dt = distance / kSteps;
        float depth[3] = {};
        for( int i = 0; i < kSteps; ++i )
        {
            const float t = ( static_cast<float>( i ) + 0.5f ) * dt;
            const float altitude = std::sqrt( std::max( r * r + t * t + 2.f * r * mu * t, 0.f ) ) - kBottomRadius;
            float extinction[3];
            AtmosphereExtinction( altitude, InAerosol, extinction );
            for( int c = 0; c < 3; ++c )
            {
                depth[c] += extinction[c] * dt;
            }
        }
        return { std::exp( -depth[0] ) * visible, std::exp( -depth[1] ) * visible, std::exp( -depth[2] ) * visible };
    }


    SkyCelestial ComputeSkyCelestial( const ProceduralSkySettings& InSettings )
    {
        // The sun along the ecliptic for the month; the time of day is local solar time, which sets
        // the sidereal time and so where every star is.
        SkyCelestial bodies;
        const float latitude = InSettings.Latitude * kDegrees;
        const float day = 30.44f * static_cast<float>( InSettings.Month ) + 15.f;
        const float sunLongitude = ( 280.46f + 0.9856474f * day ) * kDegrees;
        float sunRightAscension = 0.f;
        float sunDeclination = 0.f;
        EclipticToEquatorial( sunLongitude, sunRightAscension, sunDeclination );
        const float sidereal = ( InSettings.Time - 12.f ) * 15.f * kDegrees + sunRightAscension;

        // The equatorial frame's axes in the world (X east, Y up, Z north): the vernal equinox, the
        // point 90 degrees east of it on the celestial equator, and the celestial north pole.
        const float st = std::sin( sidereal );
        const float ct = std::cos( sidereal );
        const float sl = std::sin( latitude );
        const float cl = std::cos( latitude );
        bodies.Equatorial[0] = { -st, ct * cl, -ct * sl };
        bodies.Equatorial[1] = { ct, st * cl, -st * sl };
        bodies.Equatorial[2] = { 0.f, sl, cl };
        auto toWorld = [&bodies]( const bx::Vec3& InEquatorial ) {
            return bx::normalize( bx::add( bx::add( bx::mul( bodies.Equatorial[0], InEquatorial.x ), bx::mul( bodies.Equatorial[1], InEquatorial.y ) ), bx::mul( bodies.Equatorial[2], InEquatorial.z ) ) );
        };
        bodies.Sun = toWorld( Equatorial( sunRightAscension, sunDeclination ) );

        // The moon runs ahead of the sun along the ecliptic by its phase (full: opposite the sun).
        float moonRightAscension = 0.f;
        float moonDeclination = 0.f;
        const float moonLongitude = sunLongitude + InSettings.MoonPhase * 2.f * kPi;
        const float node = InSettings.MoonAscendingNode * kDegrees;
        const float inclination = InSettings.MoonOrbitInclination * kDegrees;
        const float argument = moonLongitude - node;
        const float x = std::cos( node ) * std::cos( argument ) - std::sin( node ) * std::sin( argument ) * std::cos( inclination );
        const float y = std::sin( node ) * std::cos( argument ) + std::cos( node ) * std::sin( argument ) * std::cos( inclination );
        const float z = std::sin( argument ) * std::sin( inclination );
        const float obliquity = 23.44f * kDegrees;
        const float eqY = y * std::cos( obliquity ) - z * std::sin( obliquity );
        const float eqZ = y * std::sin( obliquity ) + z * std::cos( obliquity );
        moonRightAscension = std::atan2( eqY, x );
        moonDeclination = std::asin( std::clamp( eqZ, -1.f, 1.f ) );
        bodies.Moon = toWorld( Equatorial( moonRightAscension, moonDeclination ) );
        const bx::Vec3 up = bx::sub( bodies.Equatorial[2], bx::mul( bodies.Moon, bx::dot( bodies.Equatorial[2], bodies.Moon ) ) );
        bodies.MoonUp = bx::length( up ) > 1e-4f ? bx::normalize( up ) : bx::Vec3{ 1.f, 0.f, 0.f };
        const float phaseAngle = kPi - std::acos( std::clamp( bx::dot( bodies.Sun, bodies.Moon ), -1.f, 1.f ) );
        bodies.MoonLitFraction = 0.5f * ( 1.f + std::cos( phaseAngle ) );
        bodies.MoonBrightness = std::max( ( std::sin( phaseAngle ) + ( kPi - phaseAngle ) * std::cos( phaseAngle ) ) / kPi, 0.f );

        bodies.GalaxyPole = toWorld( Equatorial( 192.859f * kDegrees, 27.128f * kDegrees ) );
        bodies.GalaxyCentre = toWorld( Equatorial( 266.405f * kDegrees, -28.936f * kDegrees ) );
        return bodies;
    }


    void DynamicSky::GenerateNoise( Noise& OutNoise )
    {
        // Shape: Perlin-Worley (Perlin fbm dilated by Worley cells) eroded by finer Worley octaves.
        {
            const Worley w4( 4, 11 ), w8( 8, 23 ), w16( 16, 37 ), w32( 32, 53 );
            const int n = kShapeSize;
            std::vector<float> values( static_cast<size_t>( n ) * n * n );
            ParallelFor( n, [&]( int z ) {
                for( int y = 0; y < n; ++y )
                {
                    for( int x = 0; x < n; ++x )
                    {
                        const float u = ( static_cast<float>( x ) + 0.5f ) / n;
                        const float v = ( static_cast<float>( y ) + 0.5f ) / n;
                        const float w = ( static_cast<float>( z ) + 0.5f ) / n;
                        const float perlin = PerlinFbm( u, v, w, 4, 4, 7 ) * 0.5f + 0.5f;
                        const float a = w4.Sample( u, v, w );
                        const float b = w8.Sample( u, v, w );
                        const float c = w16.Sample( u, v, w );
                        const float d = w32.Sample( u, v, w );
                        const float perlinWorley = Remap( perlin, a * 0.625f + b * 0.25f + c * 0.125f - 1.f, 1.f );
                        values[( static_cast<size_t>( z ) * n + y ) * n + x] = Remap( perlinWorley, b * 0.625f + c * 0.25f + d * 0.125f - 1.f, 1.f );
                    }
                }
            } );
            Equalize( values );
            OutNoise.Shape.resize( values.size() );
            std::transform( values.begin(), values.end(), OutNoise.Shape.begin(), ToByte );
        }

        // Detail: Worley octaves that erode the edges.
        {
            const Worley w2( 2, 71 ), w4( 4, 83 ), w8( 8, 97 );
            const int n = kDetailSize;
            std::vector<float> values( static_cast<size_t>( n ) * n * n );
            ParallelFor( n, [&]( int z ) {
                for( int y = 0; y < n; ++y )
                {
                    for( int x = 0; x < n; ++x )
                    {
                        const float u = ( static_cast<float>( x ) + 0.5f ) / n;
                        const float v = ( static_cast<float>( y ) + 0.5f ) / n;
                        const float w = ( static_cast<float>( z ) + 0.5f ) / n;
                        values[( static_cast<size_t>( z ) * n + y ) * n + x] = w2.Sample( u, v, w ) * 0.625f + w4.Sample( u, v, w ) * 0.25f + w8.Sample( u, v, w ) * 0.125f;
                    }
                }
            } );
            Equalize( values );
            OutNoise.Detail.resize( values.size() );
            std::transform( values.begin(), values.end(), OutNoise.Detail.begin(), ToByte );
        }

        // Weather: where clouds form (Perlin fbm clumped by Worley cells) and how tall they grow.
        {
            const Worley w6( 6, 131 ), w12( 12, 137 );
            const int n = kWeatherSize;
            std::vector<float> coverage( static_cast<size_t>( n ) * n );
            std::vector<float> type( coverage.size() );
            ParallelFor( n, [&]( int y ) {
                for( int x = 0; x < n; ++x )
                {
                    const float u = ( static_cast<float>( x ) + 0.5f ) / n;
                    const float v = ( static_cast<float>( y ) + 0.5f ) / n;
                    const float perlin = PerlinFbm( u, v, 0.37f, 4, 5, 151 ) * 0.5f + 0.5f;
                    const float clumps = w6.Sample( u, v, 0.5f ) * 0.65f + w12.Sample( u, v, 0.5f ) * 0.35f;
                    coverage[static_cast<size_t>( y ) * n + x] = Remap( perlin, clumps - 1.f, 1.f );
                    type[static_cast<size_t>( y ) * n + x] = PerlinFbm( u, v, 0.71f, 3, 2, 173 );
                }
            } );
            Equalize( coverage );
            Equalize( type );
            OutNoise.Weather.resize( coverage.size() * 2 );
            for( size_t i = 0; i < coverage.size(); ++i )
            {
                OutNoise.Weather[i * 2] = ToByte( coverage[i] );
                OutNoise.Weather[i * 2 + 1] = ToByte( type[i] );
            }
        }
    }


    DynamicSky::DynamicSky()
    {
        m_skyProgram = ShaderCommand( "Assets/Shaders/Sky/ProceduralSky" );
        m_transmittanceProgram = ShaderCommand( "Assets/Shaders/Post/Fullscreen", "Assets/Shaders/Sky/TransmittanceLut" );
        m_multiScatterProgram = ShaderCommand( "Assets/Shaders/Post/Fullscreen", "Assets/Shaders/Sky/MultiScatterLut" );
        m_skyViewProgram = ShaderCommand( "Assets/Shaders/Post/Fullscreen", "Assets/Shaders/Sky/SkyViewLut" );
        m_cloudProgram = ShaderCommand( "Assets/Shaders/Post/Fullscreen", "Assets/Shaders/Sky/Clouds" );
        m_captureProgram = ShaderCommand( "Assets/Shaders/Post/Fullscreen", "Assets/Shaders/Sky/SkyCapture" );

        const std::pair<bgfx::UniformHandle*, const char*> vectors[] = {
            { &u_skySun, "u_skySun" }, { &u_skySunColor, "u_skySunColor" }, { &u_skyMoon, "u_skyMoon" }, { &u_skyMoonColor, "u_skyMoonColor" },
            { &u_skyAtmosphere, "u_skyAtmosphere" }, { &u_skyCamera, "u_skyCamera" }, { &u_skyScattering, "u_skyScattering" }, { &u_skyBasisRight, "u_skyBasisRight" },
            { &u_skyBasisUp, "u_skyBasisUp" }, { &u_skyBasisForward, "u_skyBasisForward" }, { &u_skyStarsX, "u_skyStarsX" },
            { &u_skyStarsY, "u_skyStarsY" }, { &u_skyStarsZ, "u_skyStarsZ" }, { &u_skyGalaxy, "u_skyGalaxy" },
            { &u_eclipseSun, "u_eclipseSun" }, { &u_eclipseMoon, "u_eclipseMoon" }, { &u_eclipseParams, "u_eclipseParams" },
            { &u_skyGalaxyCentre, "u_skyGalaxyCentre" }, { &u_skyMoonUp, "u_skyMoonUp" }, { &u_cloudLayer, "u_cloudLayer" },
            { &u_cloudWind, "u_cloudWind" }, { &u_cloudMarch, "u_cloudMarch" }, { &u_cloudHistoryRight, "u_cloudHistoryRight" },
            { &u_cloudHistoryUp, "u_cloudHistoryUp" }, { &u_cloudHistoryForward, "u_cloudHistoryForward" }, { &u_cloudFrame, "u_cloudFrame" } };
        for( const auto& [handle, name] : vectors )
        {
            *handle = bgfx::createUniform( name, bgfx::UniformType::Vec4 );
        }
        const std::pair<bgfx::UniformHandle*, const char*> samplers[] = {
            { &s_skyTransmittance, "s_skyTransmittance" }, { &s_skyMultiScatter, "s_skyMultiScatter" }, { &s_skyView, "s_skyView" },
            { &s_skyClouds, "s_skyClouds" }, { &s_cloudHistory, "s_cloudHistory" }, { &s_cloudShape, "s_cloudShape" },
            { &s_cloudDetail, "s_cloudDetail" }, { &s_cloudWeather, "s_cloudWeather" } };
        for( const auto& [handle, name] : samplers )
        {
            *handle = bgfx::createUniform( name, bgfx::UniformType::Sampler );
        }

        m_transmittanceLut = bgfx::createTexture2D( kTransmittanceWidth, kTransmittanceHeight, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | kClamp );
        bgfx::setName( m_transmittanceLut, "Sky Transmittance" );
        m_transmittanceBuffer = bgfx::createFrameBuffer( 1, &m_transmittanceLut, false );
        m_multiScatterLut = bgfx::createTexture2D( kMultiScatterSize, kMultiScatterSize, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | kClamp );
        bgfx::setName( m_multiScatterLut, "Sky Multiple Scattering" );
        m_multiScatterBuffer = bgfx::createFrameBuffer( 1, &m_multiScatterLut, false );
        // The azimuth wraps around.
        m_skyViewLut = bgfx::createTexture2D( kSkyViewWidth, kSkyViewHeight, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_V_CLAMP );
        bgfx::setName( m_skyViewLut, "Sky View" );
        m_skyViewBuffer = bgfx::createFrameBuffer( 1, &m_skyViewLut, false );

        const uint32_t black = 0;
        m_blackTexture = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, bgfx::copy( &black, sizeof( black ) ) );
        m_blackVolume = bgfx::createTexture3D( 1, 1, 1, false, bgfx::TextureFormat::R8, BGFX_SAMPLER_NONE, bgfx::copy( &black, 1 ) );

        // The noise takes a moment; it runs while the scene loads and is waited for on first use.
        if( bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_3D )
        {
            m_noiseThread = std::thread( [this]() { GenerateNoise( m_noise ); } );
        }
    }


    DynamicSky::~DynamicSky()
    {
        if( m_noiseThread.joinable() )
        {
            m_noiseThread.join();
        }
        if( !IsGpuAlive() )
        {
            return;
        }
        for( bgfx::TextureHandle* texture : { &m_transmittanceLut, &m_multiScatterLut, &m_skyViewLut, &m_shapeTexture, &m_detailTexture, &m_weatherTexture, &m_blackTexture, &m_blackVolume } )
        {
            DestroyIfValid( *texture );
        }
        for( bgfx::FrameBufferHandle* buffer : { &m_transmittanceBuffer, &m_multiScatterBuffer, &m_skyViewBuffer } )
        {
            DestroyIfValid( *buffer );
        }
        for( bgfx::UniformHandle* uniform : { &u_skySun, &u_skySunColor, &u_skyMoon, &u_skyMoonColor, &u_skyAtmosphere, &u_skyCamera, &u_skyScattering, &u_skyBasisRight,
            &u_skyBasisUp, &u_skyBasisForward, &u_skyStarsX, &u_skyStarsY, &u_skyStarsZ, &u_skyGalaxy, &u_skyGalaxyCentre, &u_skyMoonUp, &u_eclipseSun, &u_eclipseMoon, &u_eclipseParams, &u_cloudLayer,
            &u_cloudWind, &u_cloudMarch, &u_cloudHistoryRight, &u_cloudHistoryUp, &u_cloudHistoryForward, &u_cloudFrame, &s_skyTransmittance,
            &s_skyMultiScatter, &s_skyView, &s_skyClouds, &s_cloudHistory, &s_cloudShape, &s_cloudDetail, &s_cloudWeather } )
        {
            DestroyIfValid( *uniform );
        }
    }


    bool DynamicSky::EnsureNoise()
    {
        if( m_noiseReady )
        {
            return true;
        }
        if( m_noiseThread.joinable() )
        {
            OPTICK_EVENT( "DynamicSky::WaitForCloudNoise" );
            m_noiseThread.join();
        }
        if( m_noise.Shape.empty() || m_noise.Detail.empty() || m_noise.Weather.empty() )
        {
            return false;
        }
        m_shapeTexture = bgfx::createTexture3D( kShapeSize, kShapeSize, kShapeSize, false, bgfx::TextureFormat::R8, BGFX_SAMPLER_NONE, bgfx::copy( m_noise.Shape.data(), static_cast<uint32_t>( m_noise.Shape.size() ) ) );
        bgfx::setName( m_shapeTexture, "Cloud Shape Noise" );
        m_detailTexture = bgfx::createTexture3D( kDetailSize, kDetailSize, kDetailSize, false, bgfx::TextureFormat::R8, BGFX_SAMPLER_NONE, bgfx::copy( m_noise.Detail.data(), static_cast<uint32_t>( m_noise.Detail.size() ) ) );
        bgfx::setName( m_detailTexture, "Cloud Detail Noise" );
        m_weatherTexture = bgfx::createTexture2D( kWeatherSize, kWeatherSize, false, 1, bgfx::TextureFormat::RG8, BGFX_SAMPLER_NONE, bgfx::copy( m_noise.Weather.data(), static_cast<uint32_t>( m_noise.Weather.size() ) ) );
        bgfx::setName( m_weatherTexture, "Cloud Weather" );
        m_noiseReady = true;
        return true;
    }


    bool DynamicSky::CloudsEnabled() const
    {
        return m_showsSky && m_noiseReady && m_settings.CloudCoverage > 0.f && m_settings.CloudDensity > 0.f && m_cloudProgram.IsLoaded();
    }


    void DynamicSky::ApplySettings( const ProceduralSkySettings& InSettings, const CameraData& InCamera, float InTimeSeconds )
    {
        m_settings = InSettings;
        m_settings.Clamp();
        m_timeSeconds = InTimeSeconds;
        // Other backgrounds still take their light from this sky (without directional lights) but
        // show no clouds, so the clouds mustn't shade them.
        m_showsSky = InCamera.ClearType == ClearColorType::Procedural;
        // The atmosphere needs the camera at least 10 m up (precision near the planet's surface).
        m_cameraKm = { InCamera.Position.x * 0.001f, std::max( InCamera.Position.y * 0.001f, 0.01f ), InCamera.Position.z * 0.001f };
        // Turbidity: total over clean-air optical depth (Linke). 1 is clean air, 2 a clear day.
        m_aerosol = 9.f * ( m_settings.Turbidity - 1.f );

        m_bodies = ComputeSkyCelestial( m_settings );
        m_eclipse.Sun = m_bodies.Sun;
        m_eclipse.Moon = m_bodies.Moon;
        m_eclipse.SunRadius = kSunRadius * m_settings.SunSize;
        m_eclipse.MoonRadius = kMoonRadius * m_settings.MoonSize;

        // The wind carries the cloud field towards WindDirection; the shapes slowly evolve.
        const float windAngle = m_settings.WindDirection * kDegrees;
        const float windDistance = m_settings.WindSpeed * InTimeSeconds * 0.001f;
        m_windOffset[0] = -std::sin( windAngle ) * windDistance;
        m_windOffset[1] = -std::cos( windAngle ) * windDistance;
        m_windOffset[2] = InTimeSeconds * kCloudEvolution;
        // The clouds, the moon's surface and the Milky Way all use the noise.
        if( m_showsSky )
        {
            EnsureNoise();
        }

        // The moon can be visible in daylight too. Its sky scattering and its lifted scene lighting
        // have separate scales; lunar phase comes from the same geometry as the visible terminator.
        const float lift = 1.f + ( kTwilightLift - 1.f ) * ( 1.f - Smoothstep( -0.15f, 0.1f, m_bodies.Sun.y ) );
        m_sunIlluminance = { kSunIlluminance * lift, kSunIlluminance * lift, kSunIlluminance * lift };
        const float moon = kSunIlluminance * kMoonIlluminance * m_bodies.MoonBrightness * m_settings.MoonBrightness;
        m_moonIlluminance = { moon * kMoonTint[0], moon * kMoonTint[1], moon * kMoonTint[2] };

        UpdateSceneLight();

        uint64_t key = 0x5ca1ab1eULL;
        for( const bx::Vec3* direction : { &m_bodies.Sun, &m_bodies.Moon } )
        {
            key = Mix( Mix( Mix( key, Quantize( direction->x, 120.f ) ), Quantize( direction->y, 120.f ) ), Quantize( direction->z, 120.f ) );
        }
        key = Mix( Mix( key, Quantize( std::sqrt( Luminance( m_moonIlluminance ) ), 200.f ) ), Quantize( m_aerosol, 50.f ) );
        key = Mix( key, Quantize( m_cameraKm.y, 100.f ) );
        // Eclipse contacts move much faster than the usual quantised sky directions.
        key = Mix( key, Quantize( m_directionalLights.SunVisibility, 1000.f ) );
        key = Mix( Mix( key, Quantize( m_eclipse.SunRadius, 100000.f ) ), Quantize( m_eclipse.MoonRadius, 100000.f ) );
        if( CloudsEnabled() )
        {
            key = Mix( Mix( Mix( key, Quantize( m_settings.CloudCoverage, 100.f ) ), Quantize( m_settings.CloudDensity, 50.f ) ), Quantize( m_settings.CloudAltitude, 0.01f ) );
            key = Mix( Mix( Mix( key, Quantize( m_settings.CloudThickness, 0.01f ) ), Quantize( m_windOffset[0], 4.f ) ), Quantize( m_windOffset[1], 4.f ) );
            key = Mix( Mix( key, Quantize( m_cameraKm.x, 4.f ) ), Quantize( m_cameraKm.z, 4.f ) );
        }
        m_environmentKey = key;
    }


    float DynamicSky::CloudDensity( float InX, float InAltitude, float InZ ) const
    {
        // Clouds.sh cloudDensity, with the detail erosion averaged out.
        const float base = m_settings.CloudAltitude * 0.001f;
        const float thickness = m_settings.CloudThickness * 0.001f;
        const float height = ( InAltitude - base ) / thickness;
        if( height <= 0.f || height >= 1.f )
        {
            return 0.f;
        }
        const float x = InX + m_windOffset[0];
        const float z = InZ + m_windOffset[1];
        const float coverage = std::clamp( SampleWeather( m_noise.Weather, x / kWeatherTile, z / kWeatherTile, 0 ) + m_settings.CloudCoverage * 2.f - 1.f, 0.f, 1.f );
        if( coverage <= 0.f )
        {
            return 0.f;
        }
        const float top = 0.3f + 0.7f * SampleWeather( m_noise.Weather, x / kWeatherTile, z / kWeatherTile, 1 );
        const float profile = Smoothstep( 0.f, 0.07f, height ) * ( 1.f - Smoothstep( top * 0.45f, top, height ) );
        const float shape = SampleVolume( m_noise.Shape, kShapeSize, x / kShapeTile, ( height * thickness + m_windOffset[2] ) / kShapeTile, z / kShapeTile );
        const float density = std::clamp( ( shape * profile - ( 1.f - coverage ) ) / std::max( coverage, 1e-4f ), 0.f, 1.f );
        // The detail erosion on average (it eats away about a quarter at the thin edges).
        return std::clamp( ( density - 0.25f ) / 0.75f, 0.f, 1.f );
    }


    float DynamicSky::CloudVisibility( const bx::Vec3& InLightDir ) const
    {
        // How much of the light's disc the clouds leave uncovered, seen from the camera: a short march
        // through the layer with the same density the clouds are drawn with.
        if( !CloudsEnabled() || InLightDir.y <= 0.f )
        {
            return 1.f;
        }
        const float base = m_settings.CloudAltitude * 0.001f;
        const float top = base + m_settings.CloudThickness * 0.001f;
        if( m_cameraKm.y >= top )
        {
            return 1.f;
        }
        const float rise = std::max( InLightDir.y, 0.03f );
        const float start = std::max( base - m_cameraKm.y, 0.f ) / rise;
        const float end = ( top - m_cameraKm.y ) / rise;
        constexpr int kSteps = 24;
        const float dt = ( end - start ) / kSteps;
        float depth = 0.f;
        for( int i = 0; i < kSteps; ++i )
        {
            const float t = start + ( static_cast<float>( i ) + 0.5f ) * dt;
            depth += CloudDensity( m_cameraKm.x + InLightDir.x * t, m_cameraKm.y + rise * t, m_cameraKm.z + InLightDir.z * t ) * dt;
        }
        return std::exp( -depth * kCloudExtinction * m_settings.CloudDensity );
    }


    void DynamicSky::UpdateSceneLight()
    {
        const float sunRadius = kSunRadius * m_settings.SunSize;
        const float moonRadius = kMoonRadius * m_settings.MoonSize;
        const bx::Vec3 sun = bx::mul( Scale( m_sunIlluminance, ComputeSkyTransmittance( m_cameraKm.y, m_bodies.Sun.y, m_aerosol, sunRadius ) ), CloudVisibility( m_bodies.Sun ) );
        bx::Vec3 moon = { 0.f, 0.f, 0.f };
        if( Luminance( m_moonIlluminance ) > 0.f )
        {
            moon = bx::mul( Scale( m_moonIlluminance, ComputeSkyTransmittance( m_cameraKm.y, m_bodies.Moon.y, m_aerosol, moonRadius ) ), CloudVisibility( m_bodies.Moon ) );
        }

        m_directionalLights.Sun = { Vector3( m_bodies.Sun.x, m_bodies.Sun.y, m_bodies.Sun.z ), Vector3( sun.x, sun.y, sun.z ) };
        m_directionalLights.Moon = { Vector3( m_bodies.Moon.x, m_bodies.Moon.y, m_bodies.Moon.z ), Vector3( moon.x, moon.y, moon.z ) };
        m_directionalLights.SunVisibility = m_eclipse.Visibility( { m_cameraKm.x * 1000.f, m_cameraKm.y * 1000.f, m_cameraKm.z * 1000.f } );

        // Hemisphere ambient (only until the environment probe is ready): skylight from the sun and
        // the moon (bluer than their direct light), plus the night glow.
        const bx::Vec3 sunSky = Scale( m_sunIlluminance, ComputeSkyTransmittance( m_cameraKm.y, std::max( m_bodies.Sun.y, 0.05f ), m_aerosol, sunRadius ) );
        const bx::Vec3 moonSky = Scale( m_moonIlluminance, ComputeSkyTransmittance( m_cameraKm.y, std::max( m_bodies.Moon.y, 0.05f ), m_aerosol, moonRadius ) );
        const float sunUp = Smoothstep( -0.2f, 0.3f, m_bodies.Sun.y ) * m_directionalLights.SunVisibility;
        const float moonUp = Smoothstep( -0.2f, 0.3f, m_bodies.Moon.y ) * kMoonScattering;
        const bx::Vec3 skyTint = { 0.45f, 0.62f, 1.f };
        m_ambientSky = Scale( bx::add( bx::mul( sunSky, 0.16f * sunUp ), bx::mul( moonSky, 0.16f * moonUp ) ), skyTint );
        m_ambientSky = bx::add( m_ambientSky, bx::Vec3{ kNightGlow * 4.f, kNightGlow * 5.f, kNightGlow * 8.f } );
    }


    void DynamicSky::ComputeBasis( const CameraData& InCamera, float OutRight[4], float OutUp[4], float OutForward[4], float& OutTanX, float& OutTanY ) const
    {
        const glm::mat4 inverseView = glm::inverse( InCamera.View.GetInternalMatrix() );
        glm::vec3 right = glm::normalize( glm::vec3( inverseView[0] ) );
        glm::vec3 up = glm::normalize( glm::vec3( inverseView[1] ) );
        glm::vec3 forward = glm::normalize( glm::vec3( inverseView[2] ) );
        const glm::mat4& projection = InCamera.ProjectionMatrix.GetInternalMatrix();
        if( InCamera.Projection == ProjectionType::Orthographic )
        {
            // Orthographic views show the sky as a 60 degree perspective tilted up 25 degrees, so the
            // horizon sits near the bottom of the screen.
            OutTanY = std::tan( 30.f * kDegrees );
            OutTanX = OutTanY * std::abs( projection[1][1] / ( projection[0][0] != 0.f ? projection[0][0] : 1.f ) );
            const float pitch = 25.f * kDegrees;
            const glm::vec3 tilted = glm::normalize( forward * std::cos( pitch ) + up * std::sin( pitch ) );
            up = glm::normalize( up * std::cos( pitch ) - forward * std::sin( pitch ) );
            forward = tilted;
        }
        else
        {
            OutTanX = 1.f / ( projection[0][0] != 0.f ? projection[0][0] : 1.f );
            OutTanY = 1.f / ( projection[1][1] != 0.f ? projection[1][1] : 1.f );
        }
        OutRight[0] = right.x * OutTanX; OutRight[1] = right.y * OutTanX; OutRight[2] = right.z * OutTanX; OutRight[3] = 0.f;
        OutUp[0] = up.x * OutTanY; OutUp[1] = up.y * OutTanY; OutUp[2] = up.z * OutTanY; OutUp[3] = 0.f;
        OutForward[0] = forward.x; OutForward[1] = forward.y; OutForward[2] = forward.z;
        OutForward[3] = bgfx::getCaps()->originBottomLeft ? 1.f : 0.f;
    }


    void DynamicSky::SetCommonUniforms( bool InClouds, float InSteps, float InLightSteps )
    {
        const float sun[4] = { m_bodies.Sun.x, m_bodies.Sun.y, m_bodies.Sun.z, kSunRadius * m_settings.SunSize };
        const float sunColor[4] = { m_sunIlluminance.x, m_sunIlluminance.y, m_sunIlluminance.z, kSunDiscLuminance };
        const bx::Vec3 moonPosition = bx::sub( bx::mul( m_bodies.Moon, SolarEclipse::MoonDistance ), bx::mul( m_cameraKm, 1000.f ) );
        const bx::Vec3 moonDirection = bx::normalize( moonPosition );
        const float moon[4] = { moonDirection.x, moonDirection.y, moonDirection.z, kMoonRadius * m_settings.MoonSize * SolarEclipse::MoonDistance / bx::length( moonPosition ) };
        const float moonColor[4] = { m_moonIlluminance.x * kMoonScattering, m_moonIlluminance.y * kMoonScattering, m_moonIlluminance.z * kMoonScattering, kMoonDiscLuminance * m_settings.MoonBrightness };
        const float atmosphere[4] = { m_aerosol, m_cameraKm.y, kGroundAlbedo, kNightGlow };
        const float camera[4] = { m_cameraKm.x, m_cameraKm.y, m_cameraKm.z, InClouds ? 1.f : 0.f };
        const float base = m_settings.CloudAltitude * 0.001f;
        const float layer[4] = { base, base + m_settings.CloudThickness * 0.001f, InClouds ? m_settings.CloudCoverage : 0.f, kCloudExtinction * m_settings.CloudDensity };
        const float wind[4] = { m_windOffset[0], m_windOffset[1], m_windOffset[2], kCloudAmbient };
        const float march[4] = { InSteps, InLightSteps, kCloudMaxDistance, kCloudFadeDistance };
        bgfx::setUniform( u_skySun, sun );
        bgfx::setUniform( u_skySunColor, sunColor );
        bgfx::setUniform( u_skyMoon, moon );
        bgfx::setUniform( u_skyMoonColor, moonColor );
        bgfx::setUniform( u_skyAtmosphere, atmosphere );
        bgfx::setUniform( u_skyCamera, camera );
        const float scattering[4] = { kSkyBrightness, kCloudBrightness, m_directionalLights.SunVisibility, 0.f };
        bgfx::setUniform( u_skyScattering, scattering );
        float eclipseSun[4], eclipseMoon[4], eclipseParams[4];
        m_eclipse.Uniforms( eclipseSun, eclipseMoon, eclipseParams );
        bgfx::setUniform( u_eclipseSun, eclipseSun );
        bgfx::setUniform( u_eclipseMoon, eclipseMoon );
        bgfx::setUniform( u_eclipseParams, eclipseParams );
        bgfx::setUniform( u_cloudLayer, layer );
        bgfx::setUniform( u_cloudWind, wind );
        bgfx::setUniform( u_cloudMarch, march );
    }


    void DynamicSky::BindTextures( bool InClouds )
    {
        bgfx::setTexture( 0, s_skyTransmittance, m_transmittanceLut );
        bgfx::setTexture( 1, s_skyMultiScatter, m_multiScatterLut );
        bgfx::setTexture( 2, s_skyView, m_skyViewLut );
        const bool noise = InClouds && m_noiseReady;
        bgfx::setTexture( 4, s_cloudShape, noise ? m_shapeTexture : m_blackVolume );
        bgfx::setTexture( 5, s_cloudDetail, noise ? m_detailTexture : m_blackVolume );
        bgfx::setTexture( 6, s_cloudWeather, noise ? m_weatherTexture : m_blackTexture );
    }


    void DynamicSky::Prepare( ViewAllocator& InViews, CameraData& InCamera )
    {
        OPTICK_EVENT( "DynamicSky::Prepare" );
        RenderLuts( InViews );
        RenderClouds( InViews, InCamera );
    }


    void DynamicSky::RenderLuts( ViewAllocator& InViews )
    {
        if( !m_transmittanceProgram.IsLoaded() || !m_multiScatterProgram.IsLoaded() || !m_skyViewProgram.IsLoaded() )
        {
            return;
        }
        // Transmittance and multiple scattering only change with the haze.
        if( m_lutAerosol != m_aerosol )
        {
            const bgfx::ViewId transmittanceView = InViews.Allocate( "Sky Transmittance" );
            const bgfx::ViewId multiScatterView = InViews.Allocate( "Sky Multiple Scattering" );
            if( transmittanceView != UINT16_MAX && multiScatterView != UINT16_MAX )
            {
                SetupFullscreenView( transmittanceView, m_transmittanceBuffer, kTransmittanceWidth, kTransmittanceHeight );
                SetCommonUniforms( false, 0.f, 0.f );
                SubmitFullscreen( transmittanceView, m_transmittanceProgram );

                SetupFullscreenView( multiScatterView, m_multiScatterBuffer, kMultiScatterSize, kMultiScatterSize );
                SetCommonUniforms( false, 0.f, 0.f );
                bgfx::setTexture( 0, s_skyTransmittance, m_transmittanceLut );
                SubmitFullscreen( multiScatterView, m_multiScatterProgram );
                m_lutAerosol = m_aerosol;
            }
        }

        // The sky from this camera's altitude, for this camera's sun and moon.
        const bgfx::ViewId skyView = InViews.Allocate( "Sky View" );
        if( skyView != UINT16_MAX )
        {
            SetupFullscreenView( skyView, m_skyViewBuffer, kSkyViewWidth, kSkyViewHeight );
            SetCommonUniforms( false, 0.f, 0.f );
            bgfx::setTexture( 0, s_skyTransmittance, m_transmittanceLut );
            bgfx::setTexture( 1, s_skyMultiScatter, m_multiScatterLut );
            SubmitFullscreen( skyView, m_skyViewProgram );
        }
    }


    void DynamicSky::RenderClouds( ViewAllocator& InViews, CameraData& InCamera )
    {
        if( !InCamera.Buffer )
        {
            return;
        }
        FrameBuffer& targets = *InCamera.Buffer;
        if( targets.CloudSkyKey != m_environmentKey ) targets.CloudValid = false;
        targets.CloudSkyKey = m_environmentKey;
        if( !CloudsEnabled() )
        {
            targets.CloudValid = false;
            return;
        }
        targets.EnsureCloudTargets();
        const bgfx::ViewId view = InViews.Allocate( "Clouds" );
        if( view == UINT16_MAX )
        {
            targets.CloudValid = false;
            return;
        }
        OPTICK_EVENT( "DynamicSky::RenderClouds" );
        const uint32_t read = targets.CloudIndex;
        const uint32_t write = 1 - read;
        FrameBuffer::Mip& target = targets.CloudHistory[write];
        SetupFullscreenView( view, target.Buffer, target.Width, target.Height );

        float right[4];
        float up[4];
        float forward[4];
        float tanX = 1.f;
        float tanY = 1.f;
        ComputeBasis( InCamera, right, up, forward, tanX, tanY );
        SetCommonUniforms( true, kCloudSteps, kCloudLightSteps );
        bgfx::setUniform( u_skyBasisRight, right );
        bgfx::setUniform( u_skyBasisUp, up );
        bgfx::setUniform( u_skyBasisForward, forward );

        // Last frame's camera, to find where each direction was on screen.
        const float* previous = targets.CloudPreviousBasis;
        const float historyRight[4] = { previous[0], previous[1], previous[2], targets.CloudValid ? 1.f : 0.f };
        const float historyUp[4] = { previous[3], previous[4], previous[5], kCloudHistoryWeight };
        const float historyForward[4] = { previous[6], previous[7], previous[8], 0.f };
        bgfx::setUniform( u_cloudHistoryRight, historyRight );
        bgfx::setUniform( u_cloudHistoryUp, historyUp );
        bgfx::setUniform( u_cloudHistoryForward, historyForward );

        // Sub-pixel offsets cycle through a Halton (2, 3) sequence.
        auto halton = []( uint32_t InIndex, uint32_t InBase ) {
            float result = 0.f;
            float fraction = 1.f / static_cast<float>( InBase );
            for( uint32_t i = InIndex; i > 0; i /= InBase )
            {
                result += fraction * static_cast<float>( i % InBase );
                fraction /= static_cast<float>( InBase );
            }
            return result;
        };
        const uint32_t frame = targets.CloudFrame;
        const float frameParams[4] = { static_cast<float>( frame % 1024 ), halton( frame % 16 + 1, 2 ) - 0.5f, halton( frame % 16 + 1, 3 ) - 0.5f, 0.f };
        bgfx::setUniform( u_cloudFrame, frameParams );

        BindTextures( true );
        bgfx::setTexture( 3, s_cloudHistory, targets.CloudValid ? targets.CloudHistory[read].Color : m_blackTexture );
        SubmitFullscreen( view, m_cloudProgram );

        targets.CloudIndex = write;
        targets.CloudValid = true;
        ++targets.CloudFrame;
        // right / tan(half fov) projects a direction straight to NDC (right carries one tan already).
        const float scaleX = 1.f / ( tanX * tanX );
        const float scaleY = 1.f / ( tanY * tanY );
        const float basis[9] = { right[0] * scaleX, right[1] * scaleX, right[2] * scaleX, up[0] * scaleY, up[1] * scaleY, up[2] * scaleY, forward[0], forward[1], forward[2] };
        std::copy( basis, basis + 9, targets.CloudPreviousBasis );
    }


    void DynamicSky::Draw( bgfx::ViewId InView, const CameraData& InCamera )
    {
        if( !m_skyProgram.IsLoaded() )
        {
            return;
        }
        float right[4];
        float up[4];
        float forward[4];
        float tanX = 1.f;
        float tanY = 1.f;
        ComputeBasis( InCamera, right, up, forward, tanX, tanY );
        const FrameBuffer* targets = InCamera.Buffer;
        const bool clouds = CloudsEnabled() && targets && targets->CloudValid;
        SetCommonUniforms( clouds, kCloudSteps, kCloudLightSteps );
        bgfx::setUniform( u_skyBasisRight, right );
        bgfx::setUniform( u_skyBasisUp, up );
        bgfx::setUniform( u_skyBasisForward, forward );

        const float height = targets ? static_cast<float>( std::max<uint32_t>( targets->Height, 1 ) ) : 720.f;
        const float starsX[4] = { m_bodies.Equatorial[0].x, m_bodies.Equatorial[0].y, m_bodies.Equatorial[0].z, m_settings.StarBrightness };
        const float starsY[4] = { m_bodies.Equatorial[1].x, m_bodies.Equatorial[1].y, m_bodies.Equatorial[1].z, m_timeSeconds };
        const float starsZ[4] = { m_bodies.Equatorial[2].x, m_bodies.Equatorial[2].y, m_bodies.Equatorial[2].z, 2.f * tanY / height };
        const float galaxy[4] = { m_bodies.GalaxyPole.x, m_bodies.GalaxyPole.y, m_bodies.GalaxyPole.z, kMilkyWayBrightness * m_settings.StarBrightness };
        const float galaxyCentre[4] = { m_bodies.GalaxyCentre.x, m_bodies.GalaxyCentre.y, m_bodies.GalaxyCentre.z, 0.f };
        const float moonUp[4] = { m_bodies.MoonUp.x, m_bodies.MoonUp.y, m_bodies.MoonUp.z, 0.025f * ( 1.f - m_bodies.MoonLitFraction ) * ( 1.f - Smoothstep( -0.1f, 0.05f, m_bodies.Sun.y ) ) };
        bgfx::setUniform( u_skyStarsX, starsX );
        bgfx::setUniform( u_skyStarsY, starsY );
        bgfx::setUniform( u_skyStarsZ, starsZ );
        bgfx::setUniform( u_skyGalaxy, galaxy );
        bgfx::setUniform( u_skyGalaxyCentre, galaxyCentre );
        bgfx::setUniform( u_skyMoonUp, moonUp );

        bgfx::setTexture( 0, s_skyTransmittance, m_transmittanceLut );
        bgfx::setTexture( 2, s_skyView, m_skyViewLut );
        bgfx::setTexture( 3, s_skyClouds, clouds ? targets->CloudHistory[targets->CloudIndex].Color : m_blackTexture );
        // The moon's surface and the Milky Way reuse the cloud noise.
        bgfx::setTexture( 4, s_cloudShape, m_noiseReady ? m_shapeTexture : m_blackVolume );
        bgfx::setTexture( 5, s_cloudDetail, m_noiseReady ? m_detailTexture : m_blackVolume );

        // Only where nothing has been drawn: the far plane passes against the cleared depth.
        bgfx::setState( BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL );
        screenSpaceQuad( 1.f, 1.f, 0.f, bgfx::getCaps()->originBottomLeft );
        bgfx::submit( InView, m_skyProgram.GetProgram() );
    }


    const ShaderCommand& DynamicSky::BindCapture()
    {
        const bool clouds = CloudsEnabled();
        SetCommonUniforms( clouds, kCloudCaptureSteps, kCloudCaptureLightSteps );
        BindTextures( clouds );
        return m_captureProgram;
    }
}
