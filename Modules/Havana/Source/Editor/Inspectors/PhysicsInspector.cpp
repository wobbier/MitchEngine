#include "PhysicsInspector.h"

#if USING( ME_EDITOR )
#include "Editor/ReflectionUI.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/Colliders2D.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Physics/Rigidbody2D.h"
#include <Engine/Engine.h>
#include <map>
#include <string>
#include <Widgets/PhysicsWidgets.h>

namespace PhysicsInspector
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

        // Box3D and Box2D damp once per substep: the physics cores step the fixed step in 4
        // substeps (kSubSteps in Cores/PhysicsCore.cpp and Physics2DCore.cpp).
        constexpr double kSubSteps = 4.0;
        double Substep()
        {
            return static_cast<double>( GetEngine().GetFixedTimeStep() ) / kSubSteps;
        }

        template<typename Body>
        void RegisterBody( const char* InType )
        {
            ReflectionUI::RegisterFieldDecorator( InType, "Mass", { "Mass" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
                Body& body = As<Body>( InInstance );
                const MeWidgets::NumberSpec& spec = SpecOf( InType, "Mass" );
                double mass = body.Mass;
                if( !MeWidgets::MassLadder( "Weighs Like", mass, spec, spec.HasDefault ? spec.Default : 1.0, true ) )
                {
                    return false;
                }
                body.Mass = static_cast<float>( mass );
                return true;
            } );
            ReflectionUI::RegisterFieldDecorator( InType, "LinearDamping", { "LinearDamping" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
                Body& body = As<Body>( InInstance );
                double damping = body.LinearDamping;
                if( !MeWidgets::DampingGraph( "Linear Decay", damping, SpecOf( InType, "LinearDamping" ), Substep(), "speed" ) )
                {
                    return false;
                }
                body.LinearDamping = static_cast<float>( damping );
                return true;
            } );
            ReflectionUI::RegisterFieldDecorator( InType, "AngularDamping", { "AngularDamping" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
                Body& body = As<Body>( InInstance );
                double damping = body.AngularDamping;
                if( !MeWidgets::DampingGraph( "Angular Decay", damping, SpecOf( InType, "AngularDamping" ), Substep(), "spin" ) )
                {
                    return false;
                }
                body.AngularDamping = static_cast<float>( damping );
                return true;
            } );
        }

        template<typename Collider>
        void RegisterCollider( const char* InType )
        {
            ReflectionUI::RegisterFieldDecorator( InType, "Friction", { "Friction" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
                Collider& collider = As<Collider>( InInstance );
                double friction = collider.Friction;
                if( !MeWidgets::SlideAngleRamp( "Slide Angle", friction, SpecOf( InType, "Friction" ) ) )
                {
                    return false;
                }
                collider.Friction = static_cast<float>( friction );
                return true;
            } );
            ReflectionUI::RegisterFieldDecorator( InType, "Restitution", {}, []( void* InInstance, const Reflection::TypeInfo& ) {
                MeWidgets::BouncePreview( "Bounce", As<Collider>( InInstance ).Restitution );
                return false;
            } );
        }

        const char* const kAxisNames[] = { "X", "Y", "Z" };
        const int kAxes[] = { 0, 1, 2 };
    }

    void Register()
    {
        RegisterBody<Rigidbody>( "Rigidbody" );
        RegisterBody<Rigidbody2D>( "Rigidbody2D" );

        ReflectionUI::RegisterFieldDecorator( "Rigidbody", "LockPositionZ", { "LockPositionX", "LockPositionY", "LockPositionZ" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            Rigidbody& body = As<Rigidbody>( InInstance );
            bool locks[3] = { body.LockPositionX, body.LockPositionY, body.LockPositionZ };
            if( !MeWidgets::AxisLocks( "Lock Position", locks, kAxisNames, kAxes, 3, false ) )
            {
                return false;
            }
            body.LockPositionX = locks[0];
            body.LockPositionY = locks[1];
            body.LockPositionZ = locks[2];
            return true;
        } );
        ReflectionUI::RegisterFieldDecorator( "Rigidbody", "LockRotationZ", { "LockRotationX", "LockRotationY", "LockRotationZ" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            Rigidbody& body = As<Rigidbody>( InInstance );
            bool locks[3] = { body.LockRotationX, body.LockRotationY, body.LockRotationZ };
            if( !MeWidgets::AxisLocks( "Lock Rotation", locks, kAxisNames, kAxes, 3, true ) )
            {
                return false;
            }
            body.LockRotationX = locks[0];
            body.LockRotationY = locks[1];
            body.LockRotationZ = locks[2];
            return true;
        } );
        // A 2D body turns only about Z, and has no position locks.
        ReflectionUI::RegisterFieldDecorator( "Rigidbody2D", "FreezeRotation", { "FreezeRotation" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            Rigidbody2D& body = As<Rigidbody2D>( InInstance );
            bool locks[1] = { body.FreezeRotation };
            if( !MeWidgets::AxisLocks( "Lock Rotation", locks, kAxisNames + 2, kAxes + 2, 1, true ) )
            {
                return false;
            }
            body.FreezeRotation = locks[0];
            return true;
        } );

        RegisterCollider<BoxCollider>( "BoxCollider" );
        RegisterCollider<SphereCollider>( "SphereCollider" );
        RegisterCollider<CapsuleCollider>( "CapsuleCollider" );
        RegisterCollider<MeshCollider>( "MeshCollider" );
        RegisterCollider<BoxCollider2D>( "BoxCollider2D" );
        RegisterCollider<CircleCollider2D>( "CircleCollider2D" );
        RegisterCollider<CapsuleCollider2D>( "CapsuleCollider2D" );
        RegisterCollider<PolygonCollider2D>( "PolygonCollider2D" );
        RegisterCollider<EdgeCollider2D>( "EdgeCollider2D" );
    }
}

#endif
