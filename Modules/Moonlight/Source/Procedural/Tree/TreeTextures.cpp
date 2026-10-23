#include "TreeTextures.h"
#include "TreeAsset.h"
#include "Math/Hash.h"
#include "Math/Noise.h"
#include "Path.h"
#include "Utils/BGFXUtils.h"
#include <bimg/bimg.h>
#include <bx/file.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>

namespace MitchTree
{
    namespace
    {
        constexpr float kPi = 3.14159265f;

        float Saturate( float InValue )
        {
            return std::clamp( InValue, 0.f, 1.f );
        }


        float Smoothstep( float InEdge0, float InEdge1, float InValue )
        {
            const float t = Saturate( ( InValue - InEdge0 ) / ( InEdge1 - InEdge0 ) );
            return t * t * ( 3.f - 2.f * t );
        }


        float Lerp( float InA, float InB, float InT )
        {
            return InA + ( InB - InA ) * InT;
        }


        float ToLinear( float InSrgb )
        {
            return InSrgb <= 0.04045f ? InSrgb / 12.92f : std::pow( ( InSrgb + 0.055f ) / 1.055f, 2.4f );
        }


        float ToSrgb( float InLinear )
        {
            const float value = Saturate( InLinear );
            return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow( value, 1.f / 2.4f ) - 0.055f;
        }


        struct Color
        {
            float R = 0.f, G = 0.f, B = 0.f;
        };


        Color Linear( const Vector3& InSrgb )
        {
            return { ToLinear( InSrgb.x ), ToLinear( InSrgb.y ), ToLinear( InSrgb.z ) };
        }


        Color Mix( const Color& InA, const Color& InB, float InT )
        {
            return { Lerp( InA.R, InB.R, InT ), Lerp( InA.G, InB.G, InT ), Lerp( InA.B, InB.B, InT ) };
        }


        Color Scale( const Color& InColor, float InScale )
        {
            return { InColor.R * InScale, InColor.G * InScale, InColor.B * InScale };
        }


        uint8_t Byte( float InUnit )
        {
            return static_cast<uint8_t>( std::lround( Saturate( InUnit ) * 255.f ) );
        }


        int PowerOfTwo( int InSize, int InMin, int InMax )
        {
            int size = InMin;
            while( size < InMax && size < InSize ) size *= 2;
            return size;
        }


        // A floating-point canvas: linear colour, coverage and height.
        struct Canvas
        {
            int Width = 0, Height = 0;
            std::vector<float> R, G, B, A, Relief;

            Canvas( int InWidth, int InHeight )
                : Width( InWidth ), Height( InHeight )
            {
                const size_t count = static_cast<size_t>( InWidth ) * InHeight;
                R.assign( count, 0.f ); G.assign( count, 0.f ); B.assign( count, 0.f ); A.assign( count, 0.f ); Relief.assign( count, 0.f );
            }

            size_t At( int InX, int InY ) const { return static_cast<size_t>( InY ) * Width + InX; }

            void Paint( int InX, int InY, float InCoverage, const Color& InColor, float InRelief )
            {
                if( InX < 0 || InY < 0 || InX >= Width || InY >= Height || InCoverage <= 0.f ) return;
                const size_t i = At( InX, InY );
                const float c = Saturate( InCoverage );
                R[i] = Lerp( R[i], InColor.R, c );
                G[i] = Lerp( G[i], InColor.G, c );
                B[i] = Lerp( B[i], InColor.B, c );
                Relief[i] = Lerp( Relief[i], InRelief, c );
                A[i] = A[i] + c * ( 1.f - A[i] );
            }
        };


        // Tangent-space normals from a height field (+Y along increasing v, i.e. down the image).
        Image NormalsFromHeight( const std::vector<float>& InHeight, int InWidth, int InHeightPx, float InStrength, bool InWrap )
        {
            Image out;
            out.Width = InWidth;
            out.Height = InHeightPx;
            out.Pixels.resize( static_cast<size_t>( InWidth ) * InHeightPx * 4 );
            auto sample = [&]( int x, int y ) {
                if( InWrap ) { x = ( x + InWidth ) % InWidth; y = ( y + InHeightPx ) % InHeightPx; }
                else { x = std::clamp( x, 0, InWidth - 1 ); y = std::clamp( y, 0, InHeightPx - 1 ); }
                return InHeight[static_cast<size_t>( y ) * InWidth + x];
            };
            for( int y = 0; y < InHeightPx; ++y )
            {
                for( int x = 0; x < InWidth; ++x )
                {
                    const float dx = ( sample( x + 1, y ) - sample( x - 1, y ) ) * 0.5f * InStrength;
                    const float dy = ( sample( x, y + 1 ) - sample( x, y - 1 ) ) * 0.5f * InStrength;
                    const float length = std::sqrt( dx * dx + dy * dy + 1.f );
                    uint8_t* pixel = &out.Pixels[( static_cast<size_t>( y ) * InWidth + x ) * 4];
                    pixel[0] = Byte( -dx / length * 0.5f + 0.5f );
                    pixel[1] = Byte( -dy / length * 0.5f + 0.5f );
                    pixel[2] = Byte( 1.f / length * 0.5f + 0.5f );
                    pixel[3] = 255;
                }
            }
            return out;
        }


        // Fills transparent texels with the colour of the nearest leaves (push-pull through a
        // coverage-weighted pyramid), so mips and bilinear filtering never pull in a dark fringe
        // around alpha-tested leaves. Edge texels painted over the empty canvas are unpremultiplied.
        void FillTransparent( Canvas& InOut )
        {
            struct Level { int Width, Height; std::vector<float> R, G, B, W; };
            std::vector<Level> levels;
            Level base{ InOut.Width, InOut.Height, InOut.R, InOut.G, InOut.B, InOut.A };
            levels.push_back( std::move( base ) );
            while( levels.back().Width > 1 || levels.back().Height > 1 )
            {
                const Level& fine = levels.back();
                Level coarse{ std::max( 1, fine.Width / 2 ), std::max( 1, fine.Height / 2 ), {}, {}, {}, {} };
                const size_t count = static_cast<size_t>( coarse.Width ) * coarse.Height;
                coarse.R.assign( count, 0.f ); coarse.G.assign( count, 0.f ); coarse.B.assign( count, 0.f ); coarse.W.assign( count, 0.f );
                for( int y = 0; y < fine.Height; ++y )
                {
                    for( int x = 0; x < fine.Width; ++x )
                    {
                        const size_t from = static_cast<size_t>( y ) * fine.Width + x;
                        const size_t to = static_cast<size_t>( std::min( y / 2, coarse.Height - 1 ) ) * coarse.Width + std::min( x / 2, coarse.Width - 1 );
                        coarse.R[to] += fine.R[from]; coarse.G[to] += fine.G[from]; coarse.B[to] += fine.B[from]; coarse.W[to] += fine.W[from];
                    }
                }
                levels.push_back( std::move( coarse ) );
            }
            // Pull: each level's colour is its own weighted average, falling back to its parent's.
            std::vector<float> parentR( 1, 0.f ), parentG( 1, 0.f ), parentB( 1, 0.f );
            int parentWidth = 1, parentHeight = 1;
            for( size_t k = levels.size(); k-- > 0; )
            {
                Level& level = levels[k];
                std::vector<float> r( level.R.size() ), g( level.R.size() ), b( level.R.size() );
                for( int y = 0; y < level.Height; ++y )
                {
                    for( int x = 0; x < level.Width; ++x )
                    {
                        const size_t i = static_cast<size_t>( y ) * level.Width + x;
                        const size_t p = static_cast<size_t>( std::min( y / 2, parentHeight - 1 ) ) * parentWidth + std::min( x / 2, parentWidth - 1 );
                        const bool top = k + 1 == levels.size();
                        const float w = level.W[i];
                        const float own = w > 1e-6f ? 1.f : 0.f;
                        const float fallR = top ? 0.f : parentR[p], fallG = top ? 0.f : parentG[p], fallB = top ? 0.f : parentB[p];
                        r[i] = own > 0.f ? level.R[i] / w : fallR;
                        g[i] = own > 0.f ? level.G[i] / w : fallG;
                        b[i] = own > 0.f ? level.B[i] / w : fallB;
                        if( k == 0 )
                        {
                            // Full-resolution texels: keep covered colour; recover partially covered edges.
                            const float a = InOut.A[i];
                            if( a >= 0.999f ) { r[i] = InOut.R[i]; g[i] = InOut.G[i]; b[i] = InOut.B[i]; }
                            else if( a > 1e-4f )
                            {
                                const float t = a;
                                r[i] = Lerp( top ? 0.f : parentR[p], InOut.R[i] / a, t );
                                g[i] = Lerp( top ? 0.f : parentG[p], InOut.G[i] / a, t );
                                b[i] = Lerp( top ? 0.f : parentB[p], InOut.B[i] / a, t );
                            }
                            else { r[i] = fallR; g[i] = fallG; b[i] = fallB; }
                        }
                    }
                }
                parentR.swap( r ); parentG.swap( g ); parentB.swap( b );
                parentWidth = level.Width; parentHeight = level.Height;
            }
            InOut.R.swap( parentR ); InOut.G.swap( parentG ); InOut.B.swap( parentB );
        }


        // ---------------------------------------------------------------------------------------
        // Bark
        // ---------------------------------------------------------------------------------------
        struct BarkSample
        {
            float Height = 0.5f;
            float Shade = 0.5f;     // 0 crevasse colour .. 1 surface colour
            float Tint = 0.f;       // colour noise -1..1
            float Moss = 0.f;
            float Rough = 0.f;      // roughness offset
            bool Light = false;     // birch: white bark instead of the colours
        };


        BarkSample SampleBark( const BarkTexture& InSettings, float u, float v )
        {
            const uint32_t seed = InSettings.Seed * 7919u + 17u;
            const int px = std::max( 1, static_cast<int>( std::lround( InSettings.Scale ) ) );
            const int py = std::max( 1, static_cast<int>( std::lround( InSettings.Scale / InSettings.Stretch ) ) );
            Noise::Fractal detail;
            detail.Octaves = 4;
            BarkSample s;
            s.Tint = Noise::FbmPeriodic2( u * 6.f, v * 6.f, 6, 6, seed + 3u, detail );
            const float fine = Noise::FbmPeriodic2( u * 32.f, v * 32.f, 32, 32, seed + 5u, detail );
            switch( InSettings.Style )
            {
            case BarkStyle::Furrowed:
            {
                const Noise::Cell cell = Noise::Voronoi2( u * static_cast<float>( px ), v * static_cast<float>( py ), seed, 0.85f, px, py );
                const float ridge = Smoothstep( 0.f, 0.32f, cell.F2 - cell.F1 );
                const float streak = Noise::FbmPeriodic2( u * 24.f, v * 3.f, 24, 3, seed + 7u, detail );
                s.Height = ridge * ( 0.75f + 0.25f * Hash::ToUnit( cell.Id ) ) + 0.12f * fine + 0.12f * streak;
                s.Shade = std::pow( Saturate( s.Height ), 0.8f );
                break;
            }
            case BarkStyle::Plated:
            {
                const Noise::Cell cell = Noise::Voronoi2( u * static_cast<float>( px ), v * static_cast<float>( py ), seed, 1.f, px, py );
                const float crack = Smoothstep( 0.f, 0.07f, cell.F2 - cell.F1 );
                const float layers = std::floor( Saturate( 0.5f + 0.5f * Noise::FbmPeriodic2( u * 10.f, v * 10.f, 10, 10, seed + 9u, detail ) ) * 3.f ) / 3.f;
                s.Height = crack * ( 0.55f + 0.3f * layers + 0.15f * Hash::ToUnit( cell.Id ) ) + 0.08f * fine;
                s.Shade = crack * ( 0.6f + 0.4f * layers );
                break;
            }
            case BarkStyle::Smooth:
            {
                const Noise::Cell lenticel = Noise::Voronoi2( u * static_cast<float>( px * 6 ), v * static_cast<float>( py * 10 ), seed + 11u, 0.9f, px * 6, py * 10 );
                const float dash = 1.f - Smoothstep( 0.05f, 0.14f, lenticel.F1 );
                s.Height = 0.6f + 0.12f * fine - 0.2f * dash;
                s.Shade = 0.75f + 0.25f * fine - 0.35f * dash;
                break;
            }
            case BarkStyle::Birch:
            {
                // Horizontal dark lenticels and peeling patches on white bark.
                const int lx = px * 2, ly = std::max( 1, py * 8 );
                const Noise::Cell lenticel = Noise::Voronoi2( u * static_cast<float>( lx ), v * static_cast<float>( ly ), seed + 13u, 0.9f, lx, ly );
                const float dx = ( u * static_cast<float>( lx ) - lenticel.X ) * 0.35f, dy = v * static_cast<float>( ly ) - lenticel.Y;
                const float dash = 1.f - Smoothstep( 0.06f, 0.11f, std::sqrt( dx * dx + dy * dy ) );
                const float patch = Smoothstep( 0.35f, 0.45f, Noise::FbmPeriodic2( u * 4.f, v * 12.f, 4, 12, seed + 15u, detail ) );
                s.Light = true;
                s.Shade = 1.f - std::max( dash * 0.9f, patch * 0.75f );
                s.Height = 0.65f - 0.3f * dash - 0.15f * patch + 0.05f * fine;
                break;
            }
            case BarkStyle::Ringed:
            {
                const int rings = std::max( 2, static_cast<int>( std::lround( InSettings.Scale * 1.5f ) ) );
                const float wobble = 0.08f * Noise::FbmPeriodic2( u * 4.f, v * 4.f, 4, 4, seed + 17u, detail );
                const float ring = std::pow( 0.5f + 0.5f * std::cos( 2.f * kPi * ( v * static_cast<float>( rings ) + wobble ) ), 3.f );
                const float fibres = Noise::FbmPeriodic2( u * 48.f, v * 4.f, 48, 4, seed + 19u, detail );
                s.Height = 0.65f - 0.5f * ring + 0.1f * fibres;
                s.Shade = 0.75f - 0.55f * ring + 0.15f * fibres;
                break;
            }
            case BarkStyle::Weathered:
            {
                const int wy = std::max( 1, py / 2 );
                const Noise::Cell cell = Noise::Voronoi2( u * static_cast<float>( px ), v * static_cast<float>( wy ), seed + 21u, 0.8f, px, wy );
                const float crack = Smoothstep( 0.f, 0.1f, cell.F2 - cell.F1 );
                const float grain = Noise::FbmPeriodic2( u * 40.f, v * 2.f, 40, 2, seed + 23u, detail );
                s.Height = 0.2f + 0.6f * crack + 0.15f * grain;
                s.Shade = 0.35f + 0.55f * crack + 0.1f * grain;
                s.Rough = 0.05f;
                break;
            }
            }
            if( InSettings.Moss > 0.f )
            {
                const float mossNoise = 0.5f + 0.5f * Noise::FbmPeriodic2( u * 5.f, v * 5.f, 5, 5, seed + 29u, detail );
                s.Moss = Smoothstep( 1.f - InSettings.Moss, 1.f - InSettings.Moss + 0.2f, mossNoise ) * Smoothstep( 0.25f, 0.5f, s.Height );
                s.Rough += 0.08f * s.Moss;
            }
            s.Height = Saturate( s.Height );
            return s;
        }


        // ---------------------------------------------------------------------------------------
        // Leaves
        // ---------------------------------------------------------------------------------------
        struct LeafStyle
        {
            const LeafTexture* Settings = nullptr;
            Color Base, Tip, Twig;
        };


        // Signed half-width of the blade at y (0 base .. 1 tip), as a fraction of the leaf length.
        float BladeHalfWidth( const LeafTexture& InSettings, float y )
        {
            const float aspect = InSettings.Aspect * 0.5f;
            const float body = std::pow( std::max( 0.f, std::sin( kPi * Saturate( y ) ) ), 0.75f ) * ( 1.f - 0.25f * y );
            switch( InSettings.Shape )
            {
            case LeafShape::Lobed:
            {
                const float lobes = 0.5f + 0.5f * std::cos( 2.f * kPi * static_cast<float>( InSettings.Lobes ) * 0.5f * y );
                return aspect * body * ( 1.f - InSettings.LobeDepth * lobes * Smoothstep( 0.1f, 0.25f, y ) );
            }
            case LeafShape::Serrated:
            {
                const float teeth = std::fmod( y * 30.f, 1.f );
                return aspect * body * ( 1.f - InSettings.Serration * teeth );
            }
            case LeafShape::Round:
                return aspect * 1.25f * std::pow( std::max( 0.f, std::sin( kPi * Saturate( y ) ) ), 0.55f );
            default:
                return aspect * body;
            }
        }


        // Coverage of a blade at leaf-local (x across, y along; leaf lengths), antialiased over InPixel.
        float BladeCoverage( const LeafTexture& InSettings, float x, float y, float InPixel )
        {
            if( InSettings.Shape == LeafShape::Palmate )
            {
                // A five-lobed star around the blade's centre.
                const float cx = x, cy = y - 0.45f;
                const float radius = std::sqrt( cx * cx + cy * cy );
                const float angle = std::atan2( cx, cy );
                const float lobes = 0.5f + 0.5f * std::cos( 5.f * angle );
                const float edge = 0.48f * ( 1.f - InSettings.LobeDepth * ( 1.f - std::pow( lobes, 0.6f ) ) ) * ( 1.f - InSettings.Serration * std::fmod( std::fabs( angle ) * 6.f, 1.f ) * 0.3f );
                const float notch = Smoothstep( 0.f, 0.6f, std::fabs( angle - kPi ) );  // no lobe over the stalk
                return Saturate( ( edge * std::max( notch, 0.2f ) - radius ) / InPixel + 0.5f );
            }
            if( y < -0.02f || y > 1.02f ) return 0.f;
            const float half = BladeHalfWidth( InSettings, y );
            return Saturate( ( half - std::fabs( x ) ) / InPixel + 0.5f ) * Saturate( std::min( y + 0.01f, 1.01f - y ) / InPixel + 0.5f );
        }


        // A capsule (stalk, twig or needle) from A to B with half-width InRadius, all in pixels.
        void PaintCapsule( Canvas& InCanvas, float ax, float ay, float bx, float by, float InRadius, const Color& InColor, float InRelief )
        {
            const int x0 = static_cast<int>( std::floor( std::min( ax, bx ) - InRadius - 1.f ) ), x1 = static_cast<int>( std::ceil( std::max( ax, bx ) + InRadius + 1.f ) );
            const int y0 = static_cast<int>( std::floor( std::min( ay, by ) - InRadius - 1.f ) ), y1 = static_cast<int>( std::ceil( std::max( ay, by ) + InRadius + 1.f ) );
            const float dx = bx - ax, dy = by - ay;
            const float lengthSquared = std::max( dx * dx + dy * dy, 1e-6f );
            for( int y = y0; y <= y1; ++y )
            {
                for( int x = x0; x <= x1; ++x )
                {
                    const float px = static_cast<float>( x ) + 0.5f - ax, py = static_cast<float>( y ) + 0.5f - ay;
                    const float t = Saturate( ( px * dx + py * dy ) / lengthSquared );
                    const float ex = px - dx * t, ey = py - dy * t;
                    const float distance = std::sqrt( ex * ex + ey * ey );
                    const float coverage = Saturate( InRadius - distance + 0.5f );
                    InCanvas.Paint( x, y, coverage, InColor, InRelief * ( 1.f - distance / std::max( InRadius, 0.5f ) ) );
                }
            }
        }


        // One blade with its base at (ox, oy) pointing along InAngle (radians from image up), InLength px.
        void PaintBlade( Canvas& InCanvas, const LeafStyle& InStyle, float ox, float oy, float InAngle, float InLength, Pcg32& InRng, float InLayer )
        {
            const LeafTexture& settings = *InStyle.Settings;
            const float ax = std::sin( InAngle ), ay = -std::cos( InAngle );   // along (tip direction)
            const float cx = -ay, cy = ax;                                     // across
            const float reach = InLength * ( settings.Shape == LeafShape::Palmate ? 1.f : std::max( 1.f, settings.Aspect * 1.3f ) );
            const int x0 = static_cast<int>( std::floor( ox - reach - 2.f ) ), x1 = static_cast<int>( std::ceil( ox + reach + 2.f ) );
            const int y0 = static_cast<int>( std::floor( oy - reach - 2.f ) ), y1 = static_cast<int>( std::ceil( oy + reach + 2.f ) );
            const float pixel = 1.f / InLength;
            const float jitter = settings.Variation * InRng.Signed();
            const float warm = settings.Variation * InRng.Signed() * 0.5f;
            const uint32_t seed = InRng.Next();
            Noise::Fractal mottle;
            mottle.Octaves = 3;
            for( int y = y0; y <= y1; ++y )
            {
                for( int x = x0; x <= x1; ++x )
                {
                    const float px = static_cast<float>( x ) + 0.5f - ox, py = static_cast<float>( y ) + 0.5f - oy;
                    const float along = ( px * ax + py * ay ) / InLength;
                    const float across = ( px * cx + py * cy ) / InLength;
                    const float coverage = BladeCoverage( settings, across, along, pixel );
                    if( coverage <= 0.f ) continue;
                    const float half = std::max( BladeHalfWidth( settings, along ), 0.02f );
                    const float edge = Saturate( std::fabs( across ) / half );
                    // Midrib and side veins angled toward the tip.
                    const float midrib = std::exp( -std::pow( across / 0.012f, 2.f ) );
                    const float veinPhase = std::fmod( std::fabs( ( along - std::fabs( across ) * 1.1f ) * 9.f ) + 10.f, 1.f );
                    const float side = ( 1.f - Smoothstep( 0.f, 0.08f, std::fabs( veinPhase - 0.5f ) ) ) * ( 1.f - edge );
                    const float veins = settings.Veins * std::max( midrib, side * 0.6f );
                    const float noise = Noise::Fbm2( static_cast<float>( x ) * 0.05f, static_cast<float>( y ) * 0.05f, seed, mottle );
                    Color color = Mix( InStyle.Base, InStyle.Tip, Saturate( along ) * 0.8f + 0.2f * edge );
                    color = Scale( color, ( 1.f + jitter ) * ( 1.f + 0.12f * noise ) * ( 1.f - 0.18f * edge * edge ) );
                    color.R *= 1.f + warm;
                    color = Mix( color, Scale( Mix( InStyle.Tip, Color{ 0.9f, 0.9f, 0.5f }, 0.3f ), 1.2f ), veins * 0.45f );
                    const float relief = InLayer + 0.35f * std::sqrt( std::max( 0.f, 1.f - edge * edge ) ) - 0.12f * veins + 0.03f * noise;
                    InCanvas.Paint( x, y, coverage, color, relief );
                }
            }
        }


        // One atlas cell: a twig with leaves (or a needle spray) whose stalk sits at the cell's
        // bottom centre, pointing up.
        void PaintCell( Canvas& InCanvas, const LeafStyle& InStyle, int InX0, int InY0, int InSize, uint32_t InSeed )
        {
            const LeafTexture& settings = *InStyle.Settings;
            Pcg32 rng( Hash::Of( InSeed, 0x1EAF ) );
            const float size = static_cast<float>( InSize );
            const float baseX = static_cast<float>( InX0 ) + size * 0.5f, baseY = static_cast<float>( InY0 ) + size * 0.985f;
            if( settings.Shape == LeafShape::Needles )
            {
                // A twig with needles along both sides, angled toward its tip.
                const float twigLength = size * 0.88f;
                const float bend = 0.15f * rng.Signed();
                const int tufts = 22;
                for( int i = 0; i <= tufts; ++i )
                {
                    const float t = static_cast<float>( i ) / tufts;
                    const float tx = baseX + std::sin( bend * t ) * twigLength * t, ty = baseY - twigLength * t;
                    for( int side = -1; side <= 1; side += 2 )
                    {
                        for( int k = 0; k < 2; ++k )
                        {
                            const float angle = bend * t + static_cast<float>( side ) * ( 0.55f + 0.35f * rng.Unit() );
                            const float length = size * settings.NeedleLength * ( 1.f - 0.45f * t ) * rng.Range( 0.75f, 1.f );
                            const float ex = tx + std::sin( angle ) * length, ey = ty - std::cos( angle ) * length;
                            const Color color = Scale( Mix( InStyle.Base, InStyle.Tip, rng.Unit() * 0.6f + 0.3f * t ), 1.f + settings.Variation * rng.Signed() );
                            PaintCapsule( InCanvas, tx, ty, ex, ey, std::max( 0.9f, size * 0.004f ), color, 0.4f + 0.2f * rng.Unit() );
                        }
                    }
                }
                PaintCapsule( InCanvas, baseX, baseY, baseX + std::sin( bend ) * twigLength, baseY - twigLength, std::max( 1.2f, size * 0.008f ), InStyle.Twig, 0.5f );
                return;
            }
            const int count = std::max( 1, settings.Leaves + ( settings.Leaves > 2 ? static_cast<int>( rng.Below( 3 ) ) - 1 : 0 ) );
            if( count == 1 )
            {
                const float stalk = size * 0.08f;
                PaintCapsule( InCanvas, baseX, baseY, baseX, baseY - stalk, std::max( 1.f, size * 0.006f ), InStyle.Twig, 0.3f );
                PaintBlade( InCanvas, InStyle, baseX, baseY - stalk, 0.05f * rng.Signed(), size * 0.86f, rng, 0.2f );
                return;
            }
            // Leaves alternate along a gently curved twig, shrinking toward its tip; one ends it.
            const float twigLength = size * 0.62f;
            const float bend = 0.2f * rng.Signed();
            auto twigPoint = [&]( float t, float& x, float& y ) { x = baseX + std::sin( bend * t ) * twigLength * t; y = baseY - twigLength * t; };
            const float leafLength = size * ( count <= 3 ? 0.42f : 0.36f ) / std::max( 0.6f, std::sqrt( settings.Aspect * 2.f ) );
            for( int i = 0; i < count; ++i )
            {
                const bool terminal = i == count - 1;
                const float t = terminal ? 1.f : 0.18f + 0.78f * static_cast<float>( i ) / static_cast<float>( count - 1 );
                float x = 0.f, y = 0.f;
                twigPoint( t, x, y );
                const float sideSign = ( i % 2 == 0 ) ? -1.f : 1.f;
                const float angle = terminal ? bend : bend * t + sideSign * ( 0.65f + 0.35f * rng.Unit() );
                const float length = leafLength * ( terminal ? 1.f : 1.f - 0.25f * t ) * rng.Range( 0.85f, 1.05f );
                const float stalk = length * 0.12f;
                const float sx = x + std::sin( angle ) * stalk, sy = y - std::cos( angle ) * stalk;
                PaintCapsule( InCanvas, x, y, sx, sy, std::max( 0.8f, size * 0.004f ), InStyle.Twig, 0.3f );
                PaintBlade( InCanvas, InStyle, sx, sy, angle, length, rng, 0.1f + 0.1f * static_cast<float>( i ) / count );
            }
            float tipX = 0.f, tipY = 0.f;
            twigPoint( 1.f, tipX, tipY );
            PaintCapsule( InCanvas, baseX, baseY, tipX, tipY, std::max( 1.2f, size * 0.007f ), InStyle.Twig, 0.45f );
        }


        // A palm frond filling the whole texture: a rachis up the middle, leaflets along both sides.
        void PaintFrond( Canvas& InCanvas, const LeafStyle& InStyle, uint32_t InSeed )
        {
            const LeafTexture& settings = *InStyle.Settings;
            Pcg32 rng( Hash::Of( InSeed, 0xF20D ) );
            const float w = static_cast<float>( InCanvas.Width ), h = static_cast<float>( InCanvas.Height );
            const int leaflets = 30;
            LeafTexture blade = settings;
            blade.Shape = LeafShape::Oval;
            blade.Aspect = 0.12f;
            LeafStyle bladeStyle = InStyle;
            bladeStyle.Settings = &blade;
            for( int i = 0; i < leaflets; ++i )
            {
                const float t = 0.04f + 0.94f * static_cast<float>( i ) / static_cast<float>( leaflets - 1 );
                const float y = h * ( 1.f - t );
                const float reach = 0.5f * std::pow( std::max( 0.f, std::sin( kPi * std::min( 1.f, t * 1.04f ) ) ), 0.7f );
                for( int side = -1; side <= 1; side += 2 )
                {
                    const float angle = static_cast<float>( side ) * ( 0.55f + 0.25f * t + 0.1f * rng.Signed() );
                    const float length = w * reach / std::max( 0.3f, std::sin( std::fabs( angle ) ) ) * rng.Range( 0.85f, 1.f );
                    PaintBlade( InCanvas, bladeStyle, w * 0.5f, y, angle, std::max( 4.f, length ), rng, 0.15f );
                }
            }
            PaintCapsule( InCanvas, w * 0.5f, h, w * 0.5f, h * 0.01f, std::max( 1.5f, w * 0.008f ), Mix( InStyle.Twig, InStyle.Tip, 0.4f ), 0.5f );
        }


        // ---------------------------------------------------------------------------------------
        // Mips and baking
        // ---------------------------------------------------------------------------------------
        enum class Encoding { Srgb, Linear, Normal };


        Image Downsample( const Image& InImage, Encoding InEncoding )
        {
            Image out;
            out.Width = std::max( 1, InImage.Width / 2 );
            out.Height = std::max( 1, InImage.Height / 2 );
            out.Pixels.resize( static_cast<size_t>( out.Width ) * out.Height * 4 );
            for( int y = 0; y < out.Height; ++y )
            {
                for( int x = 0; x < out.Width; ++x )
                {
                    float sum[4] = {};
                    for( int k = 0; k < 4; ++k )
                    {
                        const int sx = std::min( x * 2 + ( k & 1 ), InImage.Width - 1 ), sy = std::min( y * 2 + ( k >> 1 ), InImage.Height - 1 );
                        const uint8_t* p = &InImage.Pixels[( static_cast<size_t>( sy ) * InImage.Width + sx ) * 4];
                        for( int c = 0; c < 4; ++c )
                        {
                            const float value = p[c] / 255.f;
                            sum[c] += ( InEncoding == Encoding::Srgb && c < 3 ) ? ToLinear( value ) : InEncoding == Encoding::Normal && c < 3 ? value * 2.f - 1.f : value;
                        }
                    }
                    uint8_t* q = &out.Pixels[( static_cast<size_t>( y ) * out.Width + x ) * 4];
                    if( InEncoding == Encoding::Normal )
                    {
                        const float length = std::max( std::sqrt( sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2] ), 1e-6f );
                        for( int c = 0; c < 3; ++c ) q[c] = Byte( sum[c] / length * 0.5f + 0.5f );
                    }
                    else
                    {
                        for( int c = 0; c < 3; ++c ) q[c] = Byte( InEncoding == Encoding::Srgb ? ToSrgb( sum[c] * 0.25f ) : sum[c] * 0.25f );
                    }
                    q[3] = Byte( sum[3] * 0.25f );
                }
            }
            return out;
        }


        float Coverage( const Image& InImage, float InScale, float InCutoff )
        {
            size_t covered = 0;
            for( size_t i = 3; i < InImage.Pixels.size(); i += 4 ) covered += InImage.Pixels[i] / 255.f * InScale > InCutoff;
            return static_cast<float>( covered ) / static_cast<float>( InImage.Pixels.size() / 4 );
        }


        // Scales a mip's alpha so as much of it passes the alpha test as at full resolution
        // (Castano's coverage preservation), so leaves don't thin out with distance.
        void PreserveCoverage( Image& InOutMip, float InTarget, float InCutoff )
        {
            float low = 0.f, high = 4.f;
            for( int i = 0; i < 12; ++i )
            {
                const float middle = ( low + high ) * 0.5f;
                if( Coverage( InOutMip, middle, InCutoff ) < InTarget ) low = middle; else high = middle;
            }
            const float scale = ( low + high ) * 0.5f;
            for( size_t i = 3; i < InOutMip.Pixels.size(); i += 4 ) InOutMip.Pixels[i] = Byte( InOutMip.Pixels[i] / 255.f * scale );
        }


        bimg::ImageContainer* BuildContainer( const Image& InImage, Encoding InEncoding, float InAlphaCutoff )
        {
            if( InImage.Width <= 0 || InImage.Height <= 0 ) return nullptr;
            bimg::ImageContainer* container = bimg::imageAlloc( Moonlight::getDefaultAllocator(), bimg::TextureFormat::RGBA8,
                static_cast<uint16_t>( InImage.Width ), static_cast<uint16_t>( InImage.Height ), 1, 1, false, true );
            if( !container ) return nullptr;
            const float target = InAlphaCutoff > 0.f ? Coverage( InImage, 1.f, InAlphaCutoff ) : 0.f;
            Image level = InImage;
            for( uint8_t lod = 0; lod < container->m_numMips; ++lod )
            {
                bimg::ImageMip mip;
                if( !bimg::imageGetRawData( *container, 0, lod, container->m_data, container->m_size, mip ) ) break;
                if( lod > 0 )
                {
                    level = Downsample( level, InEncoding );
                    if( InAlphaCutoff > 0.f ) PreserveCoverage( level, target, InAlphaCutoff );
                }
                std::memcpy( const_cast<uint8_t*>( mip.m_data ), level.Pixels.data(), std::min<size_t>( mip.m_size, level.Pixels.size() ) );
            }
            return container;
        }


        // Generated sets are shared by every texture channel (and instance) of the same settings.
        struct CacheEntry
        {
            std::string Key;
            std::shared_ptr<const BarkImages> Bark;
            std::shared_ptr<const LeafImages> Leaves;
        };
        std::mutex s_cacheMutex;
        std::list<CacheEntry> s_cache;


        template<typename Settings>
        std::string CacheKey( const char* InKind, const Settings& InSettings )
        {
            json key;
            Reflection::ToJson( Settings::StaticType(), &InSettings, key );
            return std::string( InKind ) + key.dump();
        }


        CacheEntry& Cached( const std::string& InKey )
        {
            for( auto it = s_cache.begin(); it != s_cache.end(); ++it )
            {
                if( it->Key == InKey )
                {
                    s_cache.splice( s_cache.begin(), s_cache, it );
                    return s_cache.front();
                }
            }
            s_cache.push_front( { InKey, nullptr, nullptr } );
            while( s_cache.size() > 16 ) s_cache.pop_back();
            return s_cache.front();
        }
    }


    BarkImages GenerateBark( const BarkTexture& InSettings )
    {
        const int size = PowerOfTwo( InSettings.Resolution, 64, 2048 );
        BarkImages out;
        std::vector<float> height( static_cast<size_t>( size ) * size );
        out.Color.Width = out.Orm.Width = size;
        out.Color.Height = out.Orm.Height = size;
        out.Color.Pixels.resize( static_cast<size_t>( size ) * size * 4 );
        out.Orm.Pixels.resize( static_cast<size_t>( size ) * size * 4 );
        const Color surface = Linear( InSettings.Color ), crevasse = Linear( InSettings.CrevasseColor ), moss = Linear( InSettings.MossColor );
        const Color birch = Linear( Vector3( 0.86f, 0.84f, 0.8f ) );
        for( int y = 0; y < size; ++y )
        {
            for( int x = 0; x < size; ++x )
            {
                const float u = ( static_cast<float>( x ) + 0.5f ) / size, v = ( static_cast<float>( y ) + 0.5f ) / size;
                const BarkSample s = SampleBark( InSettings, u, v );
                const size_t i = static_cast<size_t>( y ) * size + x;
                height[i] = s.Height;
                Color color = Mix( crevasse, s.Light ? birch : surface, Saturate( s.Shade ) );
                color = Scale( color, 1.f + 0.12f * s.Tint );
                color = Mix( color, Scale( moss, 1.f + 0.2f * s.Tint ), s.Moss * 0.85f );
                uint8_t* c = &out.Color.Pixels[i * 4];
                c[0] = Byte( ToSrgb( color.R ) ); c[1] = Byte( ToSrgb( color.G ) ); c[2] = Byte( ToSrgb( color.B ) ); c[3] = 255;
                uint8_t* orm = &out.Orm.Pixels[i * 4];
                orm[0] = Byte( 1.f - std::min( 0.85f, 0.6f * InSettings.Depth ) * ( 1.f - s.Height ) );
                orm[1] = Byte( InSettings.Roughness + s.Rough + 0.05f * s.Tint );
                orm[2] = 0;
                orm[3] = 255;
            }
        }
        out.Normal = NormalsFromHeight( height, size, size, InSettings.Depth * static_cast<float>( size ) / 96.f, true );
        return out;
    }


    std::vector<LeafRegion> GeneratedLeafRegions( const LeafTexture& InSettings )
    {
        std::vector<LeafRegion> regions;
        if( InSettings.Shape == LeafShape::Frond ) return regions;
        const int columns = std::clamp( InSettings.Columns, 1, 4 ), rows = std::clamp( InSettings.Rows, 1, 4 );
        for( int r = 0; r < rows; ++r )
        {
            for( int c = 0; c < columns; ++c )
            {
                LeafRegion region;
                const float u0 = static_cast<float>( c ) / columns, v0 = static_cast<float>( r ) / rows;
                const float u1 = static_cast<float>( c + 1 ) / columns, v1 = static_cast<float>( r + 1 ) / rows;
                region.Rect = Vector4( u0, v0, u1, v1 );
                region.Pivot = Vector2( ( u0 + u1 ) * 0.5f, v0 + ( v1 - v0 ) * 0.985f );
                regions.push_back( region );
            }
        }
        return regions;
    }


    LeafImages GenerateLeaves( const LeafTexture& InSettings )
    {
        const int size = PowerOfTwo( InSettings.Resolution, 128, 2048 );
        Canvas canvas( size, size );
        LeafStyle style;
        style.Settings = &InSettings;
        style.Base = Linear( InSettings.Color );
        style.Tip = Linear( InSettings.TipColor );
        style.Twig = Linear( InSettings.TwigColor );
        if( InSettings.Shape == LeafShape::Frond )
        {
            PaintFrond( canvas, style, InSettings.Seed );
        }
        else
        {
            const int columns = std::clamp( InSettings.Columns, 1, 4 ), rows = std::clamp( InSettings.Rows, 1, 4 );
            const int cellWidth = size / columns, cellHeight = size / rows;
            for( int r = 0; r < rows; ++r )
            {
                for( int c = 0; c < columns; ++c )
                {
                    // Cells are square pictures; a non-square cell centres one.
                    const int cell = std::min( cellWidth, cellHeight );
                    PaintCell( canvas, style, c * cellWidth + ( cellWidth - cell ) / 2, r * cellHeight + ( cellHeight - cell ), cell, Hash::Mix32( InSettings.Seed * 131u + static_cast<uint32_t>( r * columns + c ) ) );
                }
            }
        }
        FillTransparent( canvas );
        LeafImages out;
        out.Color.Width = size;
        out.Color.Height = size;
        out.Color.Pixels.resize( static_cast<size_t>( size ) * size * 4 );
        for( size_t i = 0; i < canvas.A.size(); ++i )
        {
            out.Color.Pixels[i * 4 + 0] = Byte( ToSrgb( canvas.R[i] ) );
            out.Color.Pixels[i * 4 + 1] = Byte( ToSrgb( canvas.G[i] ) );
            out.Color.Pixels[i * 4 + 2] = Byte( ToSrgb( canvas.B[i] ) );
            out.Color.Pixels[i * 4 + 3] = Byte( canvas.A[i] );
        }
        out.Normal = NormalsFromHeight( canvas.Relief, size, size, static_cast<float>( size ) / 64.f, false );
        return out;
    }


    const std::vector<std::string>& TextureChannels()
    {
        static const std::vector<std::string> channels = { "bark-color", "bark-normal", "bark-orm", "leaf-color", "leaf-normal" };
        return channels;
    }


    std::string ChannelPath( const std::string& InRecipePath, const std::string& InChannel )
    {
        return InRecipePath + "#" + InChannel;
    }


    bimg::ImageContainer* BakeChannel( const std::string& InRecipePath, const std::string& InChannel, std::string& OutError )
    {
        TreeRecipe recipe;
        if( !LoadRecipe( Path( InRecipePath ).FullPath, recipe, OutError ) ) return nullptr;
        return BakeChannel( recipe, InChannel, OutError );
    }


    bimg::ImageContainer* BakeChannel( const TreeRecipe& InRecipe, const std::string& InChannel, std::string& OutError )
    {
        TreeRecipe recipe = InRecipe;
        recipe.Sanitize();
        const bool bark = InChannel.rfind( "bark-", 0 ) == 0;
        std::shared_ptr<const BarkImages> barkImages;
        std::shared_ptr<const LeafImages> leafImages;
        {
            // Generation is serialized: concurrent channel loads of one set wait for a single build.
            std::lock_guard<std::mutex> lock( s_cacheMutex );
            CacheEntry& entry = Cached( bark ? CacheKey( "bark", recipe.BarkTextures ) : CacheKey( "leaf", recipe.LeafTextures ) );
            if( bark )
            {
                if( !entry.Bark ) entry.Bark = std::make_shared<BarkImages>( GenerateBark( recipe.BarkTextures ) );
                barkImages = entry.Bark;
            }
            else
            {
                if( !entry.Leaves ) entry.Leaves = std::make_shared<LeafImages>( GenerateLeaves( recipe.LeafTextures ) );
                leafImages = entry.Leaves;
            }
        }
        if( InChannel == "bark-color" ) return BuildContainer( barkImages->Color, Encoding::Srgb, 0.f );
        if( InChannel == "bark-normal" ) return BuildContainer( barkImages->Normal, Encoding::Normal, 0.f );
        if( InChannel == "bark-orm" ) return BuildContainer( barkImages->Orm, Encoding::Linear, 0.f );
        if( InChannel == "leaf-color" ) return BuildContainer( leafImages->Color, Encoding::Srgb, recipe.Foliage.AlphaCutoff );
        if( InChannel == "leaf-normal" ) return BuildContainer( leafImages->Normal, Encoding::Normal, 0.f );
        OutError = "Unknown tree texture channel '" + InChannel + "'";
        return nullptr;
    }


    bool WritePng( const Image& InImage, const std::string& InFullPath, std::string& OutError )
    {
        bx::FileWriter writer;
        bx::Error error;
        if( !bx::open( &writer, InFullPath.c_str(), false, &error ) )
        {
            OutError = "Can't write " + InFullPath;
            return false;
        }
        bimg::imageWritePng( &writer, static_cast<uint32_t>( InImage.Width ), static_cast<uint32_t>( InImage.Height ), static_cast<uint32_t>( InImage.Width * 4 ),
            InImage.Pixels.data(), bimg::TextureFormat::RGBA8, false, &error );
        bx::close( &writer );
        if( !error.isOk() )
        {
            OutError = "PNG encoding failed for " + InFullPath;
            return false;
        }
        return true;
    }
}
