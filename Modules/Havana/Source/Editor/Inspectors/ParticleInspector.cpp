#include "ParticleInspector.h"

#if USING( ME_EDITOR )
#include "Editor/ReflectionUI.h"
#include "Components/Effects/ParticleSystem.h"
#include <cstdio>
#include <string>
#include <vector>
#include <Widgets/CurveWidgets.h>
#include <Widgets/GradientWidgets.h>

namespace ParticleInspector
{
    namespace
    {
        ParticleSystem& Particles( void* InInstance )
        {
            return *static_cast<ParticleSystem*>( static_cast<BaseComponent*>( InInstance ) );
        }
    }

    void Register()
    {
        // Under End Alpha, after both end fields: the runtime blends Start -> End in sRGB over
        // each particle's life (ParticleSystem::FillBatch), so the editor's gradient is exact.
        ReflectionUI::RegisterFieldDecorator( "ParticleSystem", "EndAlpha", { "StartColor", "StartAlpha", "EndColor", "EndAlpha" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            ParticleSystem& particles = Particles( InInstance );
            using MeWidgets::GradientMath::Stop;
            std::vector<Stop> stops( 2 );
            stops[0].Position = 0.f;
            stops[0].Color = { particles.StartColor.x, particles.StartColor.y, particles.StartColor.z };
            stops[0].Alpha = particles.StartAlpha;
            stops[1].Position = 1.f;
            stops[1].Color = { particles.EndColor.x, particles.EndColor.y, particles.EndColor.z };
            stops[1].Alpha = particles.EndAlpha;
            static const char* const kNames[] = { "Start", "End" };
            static std::string tooltip;
            char fade[160];
            std::snprintf( fade, sizeof( fade ), "%s.", particles.FadeIn > 0.f
                ? ( "Fade In then ramps alpha up from 0 over the first " + MeWidgets::FormatValue( Reflection::Unit::Percent, particles.FadeIn ) + " of life" ).c_str()
                : "Fade In is off" );
            tooltip = std::string( "Colour and alpha over each particle's life, blended in sRGB as the particle shader does.\n"
                "Start: Start Color and Start Alpha. End: End Color and End Alpha.\n"
                "The thin lower band starts at Start Color 2: each particle starts between the two colours.\n" ) + fade;
            const float variant[3] = { particles.StartColor2.x, particles.StartColor2.y, particles.StartColor2.z };
            MeWidgets::GradientOptions options;
            options.MinStops = 2;
            options.MaxStops = 2;
            options.FixedPositions = true;
            options.Variant = variant;
            options.Tooltip = tooltip.c_str();
            options.StopNames = kNames;
            if( !MeWidgets::GradientEditor( "Color Over Lifetime", stops, options ) || stops.size() != 2 )
            {
                return false;
            }
            particles.StartColor = Vector3( stops[0].Color[0], stops[0].Color[1], stops[0].Color[2] );
            particles.StartAlpha = stops[0].Alpha;
            particles.EndColor = Vector3( stops[1].Color[0], stops[1].Color[1], stops[1].Color[2] );
            particles.EndAlpha = stops[1].Alpha;
            return true;
        } );

        // Under End Size: size = Size x mix( 1, End Size, life ), a straight line from a fixed 1.
        ReflectionUI::RegisterFieldDecorator( "ParticleSystem", "EndSize", { "EndSize" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            ParticleSystem& particles = Particles( InInstance );
            using MeWidgets::CurveMath::Key;
            using MeWidgets::CurveMath::Tangent;
            std::vector<Key> keys( 2 );
            keys[0].X = 0.0;
            keys[0].Y = 1.0;
            keys[0].Mode = Tangent::Linear;
            keys[1].X = 1.0;
            keys[1].Y = particles.EndSize;
            keys[1].Mode = Tangent::Linear;
            MeWidgets::CurveMath::UpdateTangents( keys );
            MeWidgets::CurveOptions options;
            options.X.FieldUnit = Reflection::Unit::Percent;
            options.X.HasRange = true;
            options.X.Min = 0.0;
            options.X.Max = 1.0;
            const Reflection::FieldInfo* field = InType.FindField( "EndSize" );
            options.Y = field ? ReflectionUI::NumberSpecFor( *field ) : MeWidgets::NumberSpec();
            options.FitY = true;
            options.MinKeys = 2;
            options.MaxKeys = 2;
            options.LockX = true;
            options.LockedKeys = 1u;
            options.Tangents = false;
            options.Presets = false;
            options.KeyRow = false;
            options.Height = 5.5f;
            options.Tooltip = "Each particle's size multiplier over its life: 1 at birth (fixed), straight to End Size at death.\n"
                              "The runtime blends linearly, so the curve has no tangents to bend.";
            if( !MeWidgets::CurveEditor( "Size Over Lifetime", keys, options ) || keys.size() != 2 )
            {
                return false;
            }
            particles.EndSize = static_cast<float>( keys[1].Y );
            return true;
        } );
    }
}

#endif
