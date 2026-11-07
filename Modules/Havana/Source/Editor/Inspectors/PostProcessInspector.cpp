#include "PostProcessInspector.h"

#if USING( ME_EDITOR )
#include "Editor/ReflectionUI.h"
#include "Components/Graphics/PostProcess.h"
#include <map>
#include <string>
#include <Widgets/PostWidgets.h>

namespace PostProcessInspector
{
    namespace
    {
        PostProcess& AsPost( void* InInstance )
        {
            return *static_cast<PostProcess*>( static_cast<BaseComponent*>( InInstance ) );
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
    }

    void Register()
    {
        ReflectionUI::RegisterFieldDecorator( "PostProcess", "ExposureCompensation", { "ExposureCompensation" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            PostProcess& post = AsPost( InInstance );
            double ev = post.ExposureCompensation;
            if( !MeWidgets::ExposureLadder( "Lit Like", ev, SpecOf( InType, "ExposureCompensation" ) ) )
            {
                return false;
            }
            post.ExposureCompensation = static_cast<float>( ev );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "PostProcess", "Tonemapper", { "Tonemapper" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            PostProcess& post = AsPost( InInstance );
            int op = static_cast<int>( post.Tonemapper );
            if( !MeWidgets::TonemapCurve( "Tonemap Curve", op, post.ExposureCompensation, post.AutoExposure ) )
            {
                return false;
            }
            post.Tonemapper = static_cast<Moonlight::Tonemapper>( op );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "PostProcess", "Tint", { "Temperature", "Tint" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            PostProcess& post = AsPost( InInstance );
            double temperature = post.Temperature;
            double tint = post.Tint;
            if( !MeWidgets::WhiteBalancePad( "White Balance", temperature, tint ) )
            {
                return false;
            }
            post.Temperature = static_cast<float>( temperature );
            post.Tint = static_cast<float>( tint );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "PostProcess", "Gain", { "Lift", "Gamma", "Gain" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            PostProcess& post = AsPost( InInstance );
            double lift = post.Lift;
            double gamma = post.Gamma;
            double gain = post.Gain;
            if( !MeWidgets::LiftGammaGainCurve( "Lift Gamma Gain", lift, gamma, gain, post.Contrast ) )
            {
                return false;
            }
            post.Lift = static_cast<float>( lift );
            post.Gamma = static_cast<float>( gamma );
            post.Gain = static_cast<float>( gain );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "PostProcess", "FogDensity", { "FogDensity" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            PostProcess& post = AsPost( InInstance );
            double density = post.FogDensity;
            const float color[3] = { post.FogColor.x, post.FogColor.y, post.FogColor.z };
            if( !MeWidgets::FogVisibilityBar( "Fog Visibility", density, SpecOf( InType, "FogDensity" ), post.FogStartDistance, post.FogMaxOpacity, color, post.FogHeightFalloff ) )
            {
                return false;
            }
            post.FogDensity = static_cast<float>( density );
            return true;
        } );
    }
}

#endif
