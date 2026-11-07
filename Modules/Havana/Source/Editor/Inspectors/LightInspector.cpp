#include "LightInspector.h"

#if USING( ME_EDITOR )
#include "Editor/ReflectionUI.h"
#include "Components/Lighting/Light.h"
#include "Components/Transform.h"
#include <algorithm>
#include <map>
#include <string>
#include <Widgets/LightWidgets.h>
#include <imgui.h>

namespace LightInspector
{
    namespace
    {
        Light& AsLight( void* InInstance )
        {
            return *static_cast<Light*>( static_cast<BaseComponent*>( InInstance ) );
        }

        bool IsLocal( const Light& InLight )
        {
            return InLight.LightType != Moonlight::LightType::Directional;
        }

        bool HasCone( const Light& InLight )
        {
            return InLight.LightType == Moonlight::LightType::Spot || InLight.LightType == Moonlight::LightType::Rectangle;
        }

        const MeWidgets::NumberSpec& SpecOf( const Reflection::TypeInfo& InType, const char* InField )
        {
            static std::map<std::string, MeWidgets::NumberSpec> specs;
            auto found = specs.find( InField );
            if( found == specs.end() )
            {
                const Reflection::FieldInfo* field = InType.FindField( InField );
                found = specs.emplace( InField, field ? ReflectionUI::NumberSpecFor( *field ) : MeWidgets::NumberSpec() ).first;
            }
            return found->second;
        }

        // Looks-like references: what the multiplier on the light's linear colour roughly reads
        // as next to the engine's default exposure. Not photometric (no lumens or lux).
        const MeWidgets::Reference kLocalRungs[] = {
            { "Candle", 0.1, "A candle flame lighting the space around it" },
            { "Bulb", 1.0, "A household bulb (about 60 W) in a room" },
            { "Floodlight", 8.0, "A work light or a stage flood" },
        };
        const MeWidgets::Reference kDirectionalRungs[] = {
            { "Moonlight", 0.05, "A full moon on a manual light" },
            { "Overcast", 1.0, "A cloudy day; under a procedural sky 1 keeps the sky's own sun" },
            { "Sun", 5.0, "Clear midday sun on a manual light" },
        };
        constexpr const char* kLadderTooltip =
            "Intensity multiplies the light's linear colour. The rungs are looks-like references\n"
            "for the default exposure, not photometric values (no lumens or lux).\n"
            "Click a rung to use it; drag along the strip to scrub (log scale).";

        // The near-field area the shader softens extended emitters by. With a Transform this is the
        // engine's own command (world scale and clamps included); without one, scale 1.
        double NearField( const Light& InLight )
        {
            if( Transform* transform = InLight.Parent ? InLight.Parent->TryGetComponent<Transform>() : nullptr )
            {
                return InLight.BuildCommand( *transform ).GetNearFieldArea();
            }
            Moonlight::LightCommand command;
            command.Type = InLight.LightType;
            if( InLight.LightType == Moonlight::LightType::Bar )
            {
                const float radius = std::max( InLight.Radius, 0.001f );
                command.HalfSize = Vector3( std::max( InLight.Length, 0.f ) * 0.5f, radius, radius );
            }
            else if( InLight.LightType == Moonlight::LightType::Rectangle )
            {
                command.HalfSize = Vector3( std::max( InLight.RectangleWidth, 0.001f ) * 0.5f, std::max( InLight.RectangleHeight, 0.001f ) * 0.5f, 0.f );
            }
            else if( InLight.LightType == Moonlight::LightType::Box )
            {
                command.HalfSize = Vector3( std::max( InLight.Size.x, 0.001f ), std::max( InLight.Size.y, 0.001f ), std::max( InLight.Size.z, 0.001f ) ) * 0.5f;
            }
            return command.GetNearFieldArea();
        }
    }

    void Register()
    {
        ReflectionUI::RegisterFieldDecorator( "Light", "Color", { "Color" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            Light& light = AsLight( InInstance );
            float color[3] = { light.Color.x, light.Color.y, light.Color.z };
            if( !MeWidgets::KelvinBar( "Temperature", color ) )
            {
                return false;
            }
            light.Color = Vector3( color[0], color[1], color[2] );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "Light", "Intensity", { "Intensity" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            Light& light = AsLight( InInstance );
            const bool local = IsLocal( light );
            double value = light.Intensity;
            const bool changed = local ? MeWidgets::ReferenceLadder( "Reference", value, SpecOf( InType, "Intensity" ), kLocalRungs, IM_ARRAYSIZE( kLocalRungs ), kLadderTooltip )
                                       : MeWidgets::ReferenceLadder( "Reference", value, SpecOf( InType, "Intensity" ), kDirectionalRungs, IM_ARRAYSIZE( kDirectionalRungs ), kLadderTooltip );
            if( changed )
            {
                light.Intensity = static_cast<float>( value );
            }
            return changed;
        } );

        ReflectionUI::RegisterFieldDecorator( "Light", "Range", { "Range" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            Light& light = AsLight( InInstance );
            if( !IsLocal( light ) )
            {
                return false;
            }
            double range = light.Range;
            const float color[3] = { light.Color.x, light.Color.y, light.Color.z };
            if( !MeWidgets::FalloffGraph( "Falloff", range, SpecOf( InType, "Range" ), color, NearField( light ) ) )
            {
                return false;
            }
            light.Range = static_cast<float>( range );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "Light", "OuterConeAngle", { "InnerConeAngle", "OuterConeAngle" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            Light& light = AsLight( InInstance );
            if( !HasCone( light ) )
            {
                return false;
            }
            double inner = light.InnerConeAngle;
            double outer = light.OuterConeAngle;
            const float color[3] = { light.Color.x, light.Color.y, light.Color.z };
            if( !MeWidgets::ConeWedge( "Cone", inner, outer, color, 89.0 ) )
            {
                return false;
            }
            light.InnerConeAngle = static_cast<float>( inner );
            light.OuterConeAngle = static_cast<float>( outer );
            return true;
        } );
    }
}

#endif
