#include "CharacterInspector.h"

#if USING( ME_EDITOR )
#include "Editor/ReflectionUI.h"
#include "Components/Navigation/NavMeshAgent.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/CharacterController2D.h"
#include "Engine/ProjectSettings.h"
#include <algorithm>
#include <map>
#include <string>
#include <Widgets/CharacterWidgets.h>

namespace CharacterInspector
{
    namespace
    {
        template<typename T>
        T& As( void* InInstance )
        {
            return *static_cast<T*>( static_cast<BaseComponent*>( InInstance ) );
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

        // The default a double-click restores: the field's .Default(), else a new component's.
        double DefaultOf( const MeWidgets::NumberSpec& InSpec, double InNewValue )
        {
            return InSpec.HasDefault ? InSpec.Default : InNewValue;
        }

        // Both controllers jump with the project's gravity (PhysicsCore / Physics2DCore set their
        // worlds' gravity from it): only its Y acts, times GravityScale, sign kept. JumpArc wants
        // the downward pull, so negate: a positive gravity.y pulls up and the jump never lands.
        double Pull( double InGravityScale )
        {
            return -MeWidgets::CharacterMath::JumpGravity( ProjectSettings::Get().Gravity.y, InGravityScale );
        }

        template<typename Controller>
        void RegisterController( const char* InType )
        {
            ReflectionUI::RegisterFieldDecorator( InType, "Height", { "Height", "Radius" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
                Controller& c = As<Controller>( InInstance );
                double height = c.Height;
                double radius = c.Radius;
                if( !MeWidgets::CapsuleSilhouette( "Capsule", height, radius, SpecOf( InType, "Height" ), SpecOf( InType, "Radius" ), false ) )
                {
                    return false;
                }
                c.Height = static_cast<float>( height );
                c.Radius = static_cast<float>( radius );
                return true;
            }, []( void* InInstance, const Reflection::TypeInfo& ) {
                // Multi-select copies only the field the drag changed: keep each other capsule
                // at least as tall as its two caps, as the dragged one is.
                Controller& c = As<Controller>( InInstance );
                c.Height = std::max( c.Height, 2.f * c.Radius );
            } );
            ReflectionUI::RegisterFieldDecorator( InType, "MaxSpeed", { "MaxSpeed" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
                Controller& c = As<Controller>( InInstance );
                static const double kNew = Controller().MaxSpeed;
                const MeWidgets::NumberSpec& spec = SpecOf( InType, "MaxSpeed" );
                double speed = c.MaxSpeed;
                if( !MeWidgets::PaceStrip( "Pace", speed, spec, DefaultOf( spec, kNew ) ) )
                {
                    return false;
                }
                c.MaxSpeed = static_cast<float>( speed );
                return true;
            } );
            ReflectionUI::RegisterFieldDecorator( InType, "JumpHeight", { "JumpHeight" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
                Controller& c = As<Controller>( InInstance );
                double height = c.JumpHeight;
                if( !MeWidgets::JumpArc( "Jump", height, SpecOf( InType, "JumpHeight" ), c.MaxSpeed, Pull( c.GravityScale ) ) )
                {
                    return false;
                }
                c.JumpHeight = static_cast<float>( height );
                return true;
            } );
            ReflectionUI::RegisterFieldDecorator( InType, "SlopeLimit", { "SlopeLimit" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
                Controller& c = As<Controller>( InInstance );
                double degrees = c.SlopeLimit;
                if( !MeWidgets::SlopeWedge( "Walks Up", degrees, SpecOf( InType, "SlopeLimit" ) ) )
                {
                    return false;
                }
                c.SlopeLimit = static_cast<float>( degrees );
                return true;
            } );
        }
    }

    void Register()
    {
        RegisterController<CharacterController>( "CharacterController" );
        RegisterController<CharacterController2D>( "CharacterController2D" );

        // A navmesh agent is a cylinder (Recast's agent radius and height), so no caps to keep.
        ReflectionUI::RegisterFieldDecorator( "NavMeshAgent", "Height", { "Height", "Radius" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            NavMeshAgent& agent = As<NavMeshAgent>( InInstance );
            double height = agent.Height;
            double radius = agent.Radius;
            if( !MeWidgets::CapsuleSilhouette( "Cylinder", height, radius, SpecOf( InType, "Height" ), SpecOf( InType, "Radius" ), true ) )
            {
                return false;
            }
            agent.Height = static_cast<float>( height );
            agent.Radius = static_cast<float>( radius );
            return true;
        } );
        ReflectionUI::RegisterFieldDecorator( "NavMeshAgent", "Speed", { "Speed" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            NavMeshAgent& agent = As<NavMeshAgent>( InInstance );
            static const double kNew = NavMeshAgent().Speed;
            const MeWidgets::NumberSpec& spec = SpecOf( InType, "Speed" );
            double speed = agent.Speed;
            if( !MeWidgets::PaceStrip( "Pace", speed, spec, DefaultOf( spec, kNew ) ) )
            {
                return false;
            }
            agent.Speed = static_cast<float>( speed );
            return true;
        } );
    }
}

#endif
