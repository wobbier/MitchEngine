#include "AudioInspector.h"

#if USING( ME_EDITOR )
#include "Editor/ReflectionUI.h"
#include "Components/Audio/AudioSource.h"
#include <map>
#include <string>
#include <Widgets/AudioWidgets.h>

namespace AudioInspector
{
    namespace
    {
        AudioSource& Source( void* InInstance )
        {
            return *static_cast<AudioSource*>( static_cast<BaseComponent*>( InInstance ) );
        }

        const MeWidgets::NumberSpec& SpecOf( const Reflection::TypeInfo& InType, const char* InField )
        {
            static std::map<std::string, MeWidgets::NumberSpec> specs;
            const std::string key = InType.Name + "." + InField;
            auto found = specs.find( key );
            if( found == specs.end() )
            {
                const Reflection::FieldInfo* field = InType.FindField( InField );
                found = specs.emplace( key, field ? ReflectionUI::NumberSpecFor( *field ) : MeWidgets::NumberSpec() ).first;
            }
            return found->second;
        }
    }

    void Register()
    {
        ReflectionUI::RegisterFieldDecorator( "AudioSource", "Volume", { "Volume" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            AudioSource& source = Source( InInstance );
            double gain = source.Volume;
            // The voice's live level while it plays (after bus, distance and occlusion).
            const double live = source.IsPlaying() ? static_cast<double>( source.GetAudibility() ) : -1.0;
            if( !MeWidgets::LevelMeter( "Level", gain, SpecOf( InType, "Volume" ), live ) )
            {
                return false;
            }
            source.Volume = static_cast<float>( gain );
            return true;
        } );
        ReflectionUI::RegisterFieldDecorator( "AudioSource", "Pitch", { "Pitch" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            AudioSource& source = Source( InInstance );
            double ratio = source.Pitch;
            if( !MeWidgets::PitchKeyboard( "Keyboard", ratio, SpecOf( InType, "Pitch" ) ) )
            {
                return false;
            }
            source.Pitch = static_cast<float>( ratio );
            return true;
        } );
        ReflectionUI::RegisterFieldDecorator( "AudioSource", "SpatialBlend", { "SpatialBlend" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            AudioSource& source = Source( InInstance );
            double blend = source.SpatialBlend;
            if( !MeWidgets::SpatialBlendSlider( "Spatial", blend, SpecOf( InType, "SpatialBlend" ) ) )
            {
                return false;
            }
            source.SpatialBlend = static_cast<float>( blend );
            return true;
        } );
        // Under Rolloff, after both distance fields, so the graph edits all three.
        ReflectionUI::RegisterFieldDecorator( "AudioSource", "Rolloff", { "MinDistance", "MaxDistance", "Rolloff" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            AudioSource& source = Source( InInstance );
            int rolloff = static_cast<int>( source.Rolloff );
            double minDistance = source.MinDistance;
            double maxDistance = source.MaxDistance;
            if( !MeWidgets::RolloffGraph( "Falloff", rolloff, minDistance, maxDistance, SpecOf( InType, "MinDistance" ), SpecOf( InType, "MaxDistance" ), source.SpatialBlend ) )
            {
                return false;
            }
            source.Rolloff = static_cast<AudioRolloff>( rolloff );
            source.MinDistance = static_cast<float>( minDistance );
            source.MaxDistance = static_cast<float>( maxDistance );
            return true;
        } );
    }
}

#endif
