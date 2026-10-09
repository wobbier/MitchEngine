#include <doctest/doctest.h>
#include "Components/Physics/CharacterController2D.h"
#include "Components/Physics/Colliders2D.h"
#include "Components/Physics/PhysicsJoint2D.h"
#include "Components/Physics/Rigidbody2D.h"
#include "Components/Transform.h"
#include "Core/UpdateContext.h"
#include "Cores/Physics2DCore.h"
#include "Engine/World.h"
#include "Events/EventReceiver.h"
#include <cmath>
#include <memory>

namespace Physics2DTest
{
    constexpr float kStep = 1.f / 60.f;

    struct TestContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

    // A world with a 2D physics core. The core is declared first so it outlives the world.
    struct Scene
    {
        Physics2DCore Physics;
        std::shared_ptr<World> GameWorld;

        Scene()
        {
            GameWorld = std::make_shared<World>();
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Physics );
        }

        EntityHandle Create( const std::string& InName, const Vector3& InPosition )
        {
            EntityHandle entity = GameWorld->CreateEntity( InName );
            entity->AddComponent<Transform>().SetPosition( InPosition );
            return entity;
        }

        EntityHandle Ground()
        {
            EntityHandle ground = Create( "Ground", Vector3( 0.f, -0.5f, 0.f ) );
            ground->AddComponent<BoxCollider2D>().Size = Vector2( 40.f, 1.f );
            return ground;
        }

        EntityHandle DynamicBox( const std::string& InName, const Vector3& InPosition )
        {
            EntityHandle box = Create( InName, InPosition );
            box->AddComponent<Rigidbody2D>().Mass = 1.f;
            box->AddComponent<BoxCollider2D>().Size = Vector2( 1.f, 1.f );
            return box;
        }

        void Start()
        {
            GameWorld->Simulate();
            GameWorld->Start();
        }

        void Run( float InSeconds )
        {
            TestContext context;
            context.FixedDeltaTime = kStep;
            context.InterpolationAlpha = 1.f;
            const int steps = static_cast<int>( std::lround( InSeconds / kStep ) );
            for( int i = 0; i < steps; ++i )
            {
                GameWorld->Simulate();
                context.IsFixedStepActive = true;
                Physics.FixedUpdate( context );
                context.IsFixedStepActive = false;
                Physics.Update( context );
            }
        }
    };

    Vector3 PositionOf( EntityHandle InEntity )
    {
        return InEntity->GetComponent<Transform>().GetWorldPosition();
    }

    float AngleOf( EntityHandle InEntity )
    {
        const Quaternion q = InEntity->GetComponent<Transform>().GetWorldRotation();
        return std::atan2( 2.f * ( q.w * q.z + q.x * q.y ), 1.f - 2.f * ( q.y * q.y + q.z * q.z ) );
    }

    struct CollisionLog
        : public EventReceiver
    {
        CollisionLog()
        {
            EventManager::GetInstance().RegisterReceiver( this, { CollisionEvent::GetEventId() } );
        }

        bool OnEvent( const BaseEvent& InEvent ) override
        {
            Events.push_back( static_cast<const CollisionEvent&>( InEvent ) );
            return false;
        }

        int Count( CollisionEvent::Phase InPhase, bool InTrigger ) const
        {
            int count = 0;
            for( const CollisionEvent& event : Events )
            {
                count += event.State == InPhase && event.IsTrigger == InTrigger ? 1 : 0;
            }
            return count;
        }

        std::vector<CollisionEvent> Events;
    };
}

using namespace Physics2DTest;

TEST_CASE( "Physics 2D: a box falls onto the ground, keeps its Z and reports the contact" )
{
    CollisionLog log;
    Scene scene;
    EntityHandle ground = scene.Ground();
    EntityHandle box = scene.DynamicBox( "Box", Vector3( 0.f, 5.f, 3.f ) );
    box->GetComponent<Transform>().SetRotation( Quaternion( 0.f, 0.f, std::sin( 0.3f ), std::cos( 0.3f ) ) );   // 34 degrees
    scene.Start();
    CHECK( scene.Physics.GetBodyCount() == 2 );

    scene.Run( 3.f );
    CHECK( PositionOf( box ).y == doctest::Approx( 0.5f ).epsilon( 0.02 ) );
    CHECK( PositionOf( box ).z == doctest::Approx( 3.f ) );
    // Landed on a face: a multiple of 90 degrees.
    const float quarterTurns = AngleOf( box ) / ( 3.14159265f * 0.5f );
    CHECK( std::abs( quarterTurns - std::round( quarterTurns ) ) < 0.02f );

    REQUIRE( log.Count( CollisionEvent::Phase::Enter, false ) >= 1 );
    const CollisionEvent& first = log.Events.front();
    CHECK( first.Is2D );
    const bool pair = ( first.A == ground && first.B == box ) || ( first.A == box && first.B == ground );
    CHECK( pair );
}

TEST_CASE( "Physics 2D: queries, masks and triggers" )
{
    CollisionLog log;
    Scene scene;
    EntityHandle ground = scene.Ground();
    EntityHandle near = scene.Create( "Near", Vector3( 0.f, 2.f, 0.f ) );
    near->AddComponent<CircleCollider2D>().Radius = 0.5f;
    near->SetLayer( 3 );
    EntityHandle far = scene.Create( "Far", Vector3( 0.f, 5.f, 0.f ) );
    far->AddComponent<CircleCollider2D>().Radius = 0.5f;
    EntityHandle zone = scene.Create( "Zone", Vector3( 6.f, 2.f, 0.f ) );
    BoxCollider2D& trigger = zone->AddComponent<BoxCollider2D>();
    trigger.Size = Vector2( 4.f, 1.f );
    trigger.IsTrigger = true;
    EntityHandle ball = scene.Create( "Ball", Vector3( 6.f, 5.f, 0.f ) );
    ball->AddComponent<Rigidbody2D>();
    ball->AddComponent<CircleCollider2D>().Radius = 0.25f;
    scene.Start();
    scene.Run( kStep );

    RaycastHit hit;
    REQUIRE( scene.Physics.Raycast( Vector2( 0.f, 10.f ), Vector2( 0.f, -1.f ), 100.f, hit ) );
    CHECK( hit.Entity == far );
    CHECK( hit.Distance == doctest::Approx( 4.5f ) );
    CHECK( hit.Normal.y == doctest::Approx( 1.f ) );

    const std::vector<RaycastHit> all = scene.Physics.RaycastAll( Vector2( 0.f, 10.f ), Vector2( 0.f, -1.f ), 100.f );
    REQUIRE( all.size() == 3 );
    CHECK( all[0].Entity == far );
    CHECK( all[1].Entity == near );
    CHECK( all[2].Entity == ground );

    REQUIRE( scene.Physics.Raycast( Vector2( 0.f, 10.f ), Vector2( 0.f, -1.f ), 100.f, hit, PhysicsLayers::Bit( 3 ) ) );
    CHECK( hit.Entity == near );
    CHECK_FALSE( scene.Physics.Linecast( Vector2( 0.f, 10.f ), Vector2( 0.f, 6.f ), hit ) );
    CHECK_FALSE( scene.Physics.Raycast( Vector2( 0.9f, 10.f ), Vector2( 0.f, -1.f ), 7.f, hit ) );
    REQUIRE( scene.Physics.CircleCast( Vector2( 0.9f, 10.f ), 0.5f, Vector2( 0.f, -1.f ), 7.f, hit ) );
    CHECK( hit.Entity == far );

    // Rays ignore the trigger volume.
    REQUIRE( scene.Physics.Raycast( Vector2( 7.5f, 3.f ), Vector2( 0.f, -1.f ), 10.f, hit ) );
    CHECK( hit.Entity == ground );

    CHECK( scene.Physics.OverlapCircle( Vector2( 0.f, 2.f ), 1.f ).size() == 1 );
    CHECK( scene.Physics.OverlapBox( Vector2( 0.f, 3.5f ), Vector2( 0.2f, 2.f ), 0.f ).size() == 2 );
    CHECK( scene.Physics.OverlapBox( Vector2( 0.f, 3.5f ), Vector2( 0.2f, 2.f ), 0.f, PhysicsLayers::Bit( 0 ) ).size() == 1 );
    const std::vector<EntityHandle> atPoint = scene.Physics.OverlapPoint( Vector2( 0.1f, 5.1f ) );
    REQUIRE( atPoint.size() == 1 );
    CHECK( atPoint[0] == far );

    // The ball falls through the trigger and lands on the ground.
    scene.Run( 2.f );
    CHECK( PositionOf( ball ).y == doctest::Approx( 0.25f ).epsilon( 0.03 ) );
    CHECK( log.Count( CollisionEvent::Phase::Enter, true ) == 1 );
    CHECK( log.Count( CollisionEvent::Phase::Exit, true ) == 1 );
}

TEST_CASE( "Physics 2D: polygons, edges and compound bodies" )
{
    // More than 8 points collide as a simplified convex hull.
    std::vector<Vector2> circle;
    for( int i = 0; i < 24; ++i )
    {
        const float a = i * 2.f * 3.14159265f / 24.f;
        circle.push_back( Vector2( std::cos( a ), std::sin( a ) ) );
    }
    circle.push_back( Vector2( 0.f, 0.f ) );   // interior points are dropped
    const std::vector<Vector2> hull = Collider2DUtils::ConvexHull( circle, 8 );
    CHECK( hull.size() == 8 );

    Scene scene;
    // A V-shaped valley of edge segments.
    EntityHandle valley = scene.Create( "Valley", Vector3( 0.f, 0.f, 0.f ) );
    valley->AddComponent<EdgeCollider2D>().Points = { Vector2( -10.f, 5.f ), Vector2( 0.f, 0.f ), Vector2( 10.f, 5.f ) };
    EntityHandle wheel = scene.Create( "Wheel", Vector3( -6.f, 6.f, 0.f ) );
    wheel->AddComponent<Rigidbody2D>();
    wheel->AddComponent<PolygonCollider2D>().Points = circle;

    // A dumbbell: two child circles on one body.
    EntityHandle dumbbell = scene.Create( "Dumbbell", Vector3( 5.f, 8.f, 0.f ) );
    dumbbell->AddComponent<Rigidbody2D>().Mass = 2.f;
    for( float side : { -1.f, 1.f } )
    {
        EntityHandle weight = scene.Create( "Weight", Vector3( side * 0.75f, 0.f, 0.f ) );
        weight->GetComponent<Transform>().SetParent( dumbbell->GetComponent<Transform>() );
        weight->AddComponent<CircleCollider2D>().Radius = 0.3f;
    }
    scene.Start();
    scene.Run( kStep );
    CHECK( scene.Physics.GetBodyCount() == 3 );
    CHECK( dumbbell->GetComponent<Rigidbody2D>().GetBodyMass() == doctest::Approx( 2.f ) );

    // Everything rolls / slides down into the valley.
    scene.Run( 10.f );
    CHECK( std::abs( PositionOf( wheel ).x ) < 3.f );
    CHECK( PositionOf( wheel ).y < 3.f );
    CHECK( PositionOf( wheel ).y > 0.5f );
    CHECK( PositionOf( dumbbell ).y < 3.f );
}

TEST_CASE( "Physics 2D: hinges, ropes, sliders and breaking joints" )
{
    Scene scene;
    // A 2 m rope from (0, 5) to a ball starting level with it.
    EntityHandle bob = scene.Create( "Bob", Vector3( 2.f, 5.f, 0.f ) );
    bob->AddComponent<Rigidbody2D>();
    bob->AddComponent<CircleCollider2D>().Radius = 0.2f;
    PhysicsJoint2D& rope = bob->AddComponent<PhysicsJoint2D>();
    rope.Type = JointType2D::Distance;
    rope.ConnectedAnchor = Vector2( 0.f, 5.f );

    // A motorised paddle on a hinge at its centre.
    EntityHandle paddle = scene.Create( "Paddle", Vector3( 10.f, 5.f, 0.f ) );
    paddle->AddComponent<Rigidbody2D>().GravityScale = 0.f;
    paddle->AddComponent<BoxCollider2D>().Size = Vector2( 2.f, 0.2f );
    PhysicsJoint2D& hinge = paddle->AddComponent<PhysicsJoint2D>();
    hinge.Type = JointType2D::Hinge;
    hinge.UseMotor = true;
    hinge.MotorSpeed = 90.f;
    hinge.MaxMotorForce = 1000.f;

    // An elevator on a vertical slider with limits.
    EntityHandle lift = scene.Create( "Lift", Vector3( -10.f, 5.f, 0.f ) );
    lift->AddComponent<Rigidbody2D>();
    lift->AddComponent<BoxCollider2D>();
    PhysicsJoint2D& slider = lift->AddComponent<PhysicsJoint2D>();
    slider.Type = JointType2D::Slider;
    slider.Axis = Vector2( 0.f, 1.f );
    slider.UseLimits = true;
    slider.LowerLimit = -1.f;
    slider.UpperLimit = 1.f;

    // A heavy weight on a weak weld.
    EntityHandle weight = scene.Create( "Weight", Vector3( -20.f, 5.f, 0.f ) );
    weight->AddComponent<Rigidbody2D>().Mass = 50.f;
    weight->AddComponent<BoxCollider2D>();
    PhysicsJoint2D& weld = weight->AddComponent<PhysicsJoint2D>();
    weld.Type = JointType2D::Fixed;
    weld.BreakForce = 10.f;

    scene.Start();
    scene.Run( 1.f );

    const Vector3 bobPosition = PositionOf( bob );
    CHECK( std::hypot( bobPosition.x, bobPosition.y - 5.f ) == doctest::Approx( 2.f ).epsilon( 0.02 ) );
    CHECK( bobPosition.y < 3.6f );

    // +90 deg/s (counter-clockwise) for one second, about its centre, which stays put.
    CHECK( AngleOf( paddle ) == doctest::Approx( 3.14159265f * 0.5f ).epsilon( 0.05 ) );
    CHECK( PositionOf( paddle ).x == doctest::Approx( 10.f ).epsilon( 0.001 ) );

    // The lift slid down to its lower limit and stayed on its axis.
    CHECK( PositionOf( lift ).y == doctest::Approx( 4.f ).epsilon( 0.01 ) );
    CHECK( PositionOf( lift ).x == doctest::Approx( -10.f ).epsilon( 0.001 ) );

    CHECK( weld.IsBroken() );
    CHECK( PositionOf( weight ).y < 2.f );
}

TEST_CASE( "Physics 2D: the platformer controller runs, stops at walls, jumps and has coyote time" )
{
    Scene scene;
    EntityHandle floor = scene.Create( "Floor", Vector3( 0.f, -0.5f, 0.f ) );
    floor->AddComponent<BoxCollider2D>().Size = Vector2( 10.f, 1.f );   // ends at x = 5
    EntityHandle wall = scene.Create( "Wall", Vector3( -4.f, 1.f, 0.f ) );
    wall->AddComponent<BoxCollider2D>().Size = Vector2( 1.f, 2.f );     // face at x = -3.5
    EntityHandle player = scene.Create( "Player", Vector3( 0.f, 0.85f, 2.f ) );
    CharacterController2D& controller = player->AddComponent<CharacterController2D>();
    scene.Start();

    scene.Run( 0.25f );
    CHECK( controller.IsOnGround() );
    CHECK( PositionOf( player ).y == doctest::Approx( 0.8f ).epsilon( 0.02 ) );
    CHECK( PositionOf( player ).z == doctest::Approx( 2.f ) );

    controller.SetMoveInput( -1.f );
    scene.Run( 1.5f );
    CHECK( PositionOf( player ).x == doctest::Approx( -3.2f ).epsilon( 0.02 ) );   // radius 0.3 from the wall face
    CHECK( controller.IsOnGround() );

    controller.SetMoveInput( 0.f );
    scene.Run( 0.5f );
    controller.Jump();
    scene.Run( 0.2f );
    CHECK_FALSE( controller.IsOnGround() );
    CHECK( PositionOf( player ).y > 1.6f );
    scene.Run( 1.5f );
    CHECK( controller.IsOnGround() );

    // Run off the right edge and jump just after leaving it.
    controller.SetMoveInput( 1.f );
    for( int i = 0; i < 300 && PositionOf( player ).x < 5.4f; ++i )
    {
        scene.Run( kStep );
    }
    CHECK_FALSE( controller.IsOnGround() );
    controller.Jump();
    scene.Run( kStep );
    CHECK( controller.GetVelocity().y > 3.f );
    // Only once.
    scene.Run( 0.05f );
    const float vy = controller.GetVelocity().y;
    controller.Jump();
    scene.Run( kStep );
    CHECK( controller.GetVelocity().y < vy );
}

TEST_CASE( "Physics 2D: bodies keep their X/Y tilt while spinning about Z" )
{
    Scene scene;
    scene.Ground();
    // A wheel modelled like a cylinder on its side: its local Y axis points along world Z.
    EntityHandle wheel = scene.Create( "Wheel", Vector3( 0.f, 2.f, 0.f ) );
    wheel->GetComponent<Transform>().SetRotation( Quaternion( std::sin( 0.785398f ), 0.f, 0.f, std::cos( 0.785398f ) ) );   // 90 degrees about X
    wheel->AddComponent<Rigidbody2D>();
    wheel->AddComponent<CircleCollider2D>().Radius = 0.5f;
    scene.Start();
    scene.Run( kStep );
    wheel->GetComponent<Rigidbody2D>().SetVelocity( Vector2( 3.f, 0.f ) );
    scene.Run( 1.5f );

    // It rolled (spun about Z) and its axis still points along Z.
    CHECK( PositionOf( wheel ).x > 2.f );
    CHECK( std::abs( AngleOf( wheel ) ) > 0.5f );
    const Vector3 axis = wheel->GetComponent<Transform>().GetWorldRotation() * Vector3( 0.f, 1.f, 0.f );
    CHECK( std::abs( axis.z ) == doctest::Approx( 1.f ).epsilon( 0.001 ) );
}

