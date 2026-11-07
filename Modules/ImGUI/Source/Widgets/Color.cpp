#include "Color.h"
#include <algorithm>
#include <cmath>

namespace MeWidgets::ColorTemperature
{
    std::array<float, 3> ToSrgb( double InKelvin )
    {
        // Tanner Helland's piecewise fit, in 8-bit units of the temperature in hundreds of kelvin.
        const double t = std::clamp( InKelvin, 1000.0, 40000.0 ) / 100.0;
        double r = 255.0;
        double g = 255.0;
        double b = 255.0;
        if( t > 66.0 )
        {
            r = 329.698727446 * std::pow( t - 60.0, -0.1332047592 );
            g = 288.1221695283 * std::pow( t - 60.0, -0.0755148492 );
        }
        else
        {
            g = 99.4708025861 * std::log( t ) - 161.1195681661;
            b = t <= 19.0 ? 0.0 : 138.5177312231 * std::log( t - 10.0 ) - 305.0447927307;
        }
        std::array<float, 3> color = { static_cast<float>( std::clamp( r, 0.0, 255.0 ) / 255.0 ),
            static_cast<float>( std::clamp( g, 0.0, 255.0 ) / 255.0 ), static_cast<float>( std::clamp( b, 0.0, 255.0 ) / 255.0 ) };
        const float peak = std::max( { color[0], color[1], color[2] } );
        if( peak > 0.f )
        {
            for( float& channel : color ) channel /= peak;
        }
        return color;
    }

    namespace
    {
        // Largest channel difference between a normalised colour and the curve at InKelvin.
        float Distance( const std::array<float, 3>& InColor, double InKelvin )
        {
            const std::array<float, 3> curve = ToSrgb( InKelvin );
            return std::max( { std::abs( curve[0] - InColor[0] ), std::abs( curve[1] - InColor[1] ), std::abs( curve[2] - InColor[2] ) } );
        }
    }

    bool FromSrgb( const std::array<float, 3>& InSrgb, double& OutKelvin, float InTolerance )
    {
        const float peak = std::max( { InSrgb[0], InSrgb[1], InSrgb[2] } );
        if( !( peak > 1e-4f ) || !std::isfinite( peak ) )
        {
            return false;
        }
        const std::array<float, 3> color = { InSrgb[0] / peak, InSrgb[1] / peak, InSrgb[2] / peak };
        // Coarse search in log temperature, then golden-section refinement around the best sample.
        constexpr int kSamples = 240;
        const double logMin = std::log( kMinKelvin );
        const double logMax = std::log( kMaxKelvin );
        int best = 0;
        float bestDistance = 1e9f;
        for( int i = 0; i <= kSamples; ++i )
        {
            const float d = Distance( color, std::exp( logMin + ( logMax - logMin ) * i / kSamples ) );
            if( d < bestDistance )
            {
                bestDistance = d;
                best = i;
            }
        }
        double a = logMin + ( logMax - logMin ) * std::max( best - 1, 0 ) / kSamples;
        double b = logMin + ( logMax - logMin ) * std::min( best + 1, kSamples ) / kSamples;
        constexpr double kGolden = 0.6180339887498949;
        for( int i = 0; i < 40; ++i )
        {
            const double c = b - ( b - a ) * kGolden;
            const double d = a + ( b - a ) * kGolden;
            if( Distance( color, std::exp( c ) ) < Distance( color, std::exp( d ) ) ) b = d;
            else a = c;
        }
        const double kelvin = std::exp( ( a + b ) * 0.5 );
        if( Distance( color, kelvin ) > InTolerance )
        {
            return false;
        }
        OutKelvin = kelvin;
        return true;
    }

    const Preset* Presets( int& OutCount )
    {
        static const Preset presets[] = {
            { "Candle", 1900.0 },
            { "Tungsten", 2700.0 },
            { "Halogen", 3200.0 },
            { "Daylight", 5600.0 },
            { "Overcast", 6500.0 },
            { "Shade", 7500.0 },
            { "Blue sky", 10000.0 },
        };
        OutCount = static_cast<int>( sizeof( presets ) / sizeof( presets[0] ) );
        return presets;
    }
}
