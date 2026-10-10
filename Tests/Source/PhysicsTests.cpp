#include <doctest/doctest.h>
#include "Audio/AudioOcclusion.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/PhysicsJoint.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Events/EventReceiver.h"
#include "World/SceneSerializer.h"
#include <cmath>
#include <memory>

namespace PhysicsTest
{
    constexpr float kStep = 1.f / 60.f;

    // The engine owns frame timing; tests drive it directly.
    struct TestContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

    // A world with a physics core. The core is declared first so it outlives the world.
    struct Scene
    {
        PhysicsCore Physics;
        std::shared_ptr<World> GameWorld;

        Scene()
        {
            GameWorld = std::make_shared<World>();
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Physics );
        }

        EntityHandle Create( const std::string& InName, const Vector3& InPosition, const Vector3& InScale = Vector3( 1.f, 1.f, 1.f ) )
        {
            EntityHandle entity = GameWorld->CreateEntity( InName );
            Transform& transform = entity->AddComponent<Transform>();
            transform.SetPosition( InPosition );
            transform.SetScale( InScale );
            return entity;
        }

        EntityHandle Ground( const std::string& InName = "Ground" )
        {
            EntityHandle ground = Create( InName, Vector3( 0.f, -0.5f, 0.f ) );
            ground->AddComponent<BoxCollider>().Size = Vector3( 40.f, 1.f, 40.f );
            return ground;
        }

        EntityHandle DynamicBox( const std::string& InName, const Vector3& InPosition )
        {
            EntityHandle box = Create( InName, InPosition );
            box->AddComponent<Rigidbody>().Mass = 1.f;
            box->AddComponent<BoxCollider>().Size = Vector3( 1.f, 1.f, 1.f );
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

using namespace PhysicsTest;

TEST_CASE( "Physics: a dynamic box falls, lands on a static floor and reports the contact" )
{
    CollisionLog log;
    Scene scene;
    EntityHandle ground = scene.Ground();
    EntityHandle box = scene.DynamicBox( "Box", Vector3( 0.f, 5.f, 0.f ) );
    scene.Start();
    CHECK( scene.Physics.GetBodyCount() == 2 );

    scene.Run( 0.5f );
    CHECK( PositionOf( box ).y < 4.f );   // falling
    CHECK( box->GetComponent<Rigidbody>().GetVelocity().y < -1.f );

    scene.Run( 2.5f );
    CHECK( PositionOf( box ).y == doctest::Approx( 0.5f ).epsilon( 0.02 ) );
    CHECK( std::abs( PositionOf( box ).x ) < 0.05f );

    REQUIRE( log.Count( CollisionEvent::Phase::Enter, false ) >= 1 );
    const CollisionEvent& first = log.Events.front();
    const bool pair = ( first.A == ground && first.B == box ) || ( first.A == box && first.B == ground );
    CHECK( pair );
    CHECK( first.Point.y == doctest::Approx( 0.f ).epsilon( 0.05 ) );
}

TEST_CASE( "Physics: static, kinematic and edit-mode bodies follow their Transforms" )
{
    Scene scene;
    scene.Ground();
    EntityHandle box = scene.DynamicBox( "Box", Vector3( 0.f, 5.f, 0.f ) );
    scene.GameWorld->Simulate();

    // Edit mode (world not started): nothing falls, but moving a Transform moves its body.
    TestContext context;
    context.FixedDeltaTime = kStep;
    scene.Physics.FixedUpdate( context );
    scene.Physics.Update( context );
    CHECK( PositionOf( box ).y == doctest::Approx( 5.f ) );
    box->GetComponent<Transform>().SetPosition( Vector3( 3.f, 2.f, 0.f ) );
    scene.Physics.Update( context );
    RaycastHit hit;
    REQUIRE( scene.Physics.Raycast( Vector3( 3.f, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, hit ) );
    CHECK( hit.Entity == box );
    CHECK( hit.Position.y == doctest::Approx( 2.5f ) );

    // Kinematic bodies are moved, not simulated.
    EntityHandle platform = scene.Create( "Platform", Vector3( -5.f, 1.f, 0.f ) );
    platform->AddComponent<Rigidbody>().Type = BodyType::Kinematic;
    platform->AddComponent<BoxCollider>();
    scene.Start();
    scene.Run( 0.5f );
    CHECK( PositionOf( platform ).y == doctest::Approx( 1.f ) );
    platform->GetComponent<Rigidbody>().MoveTo( Vector3( -5.f, 3.f, 0.f ), Quaternion() );
    scene.Run( 0.1f );
    CHECK( PositionOf( platform ).y == doctest::Approx( 3.f ) );
    RaycastHit platformHit;
    REQUIRE( scene.Physics.Raycast( Vector3( -5.f, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, platformHit ) );
    CHECK( platformHit.Position.y == doctest::Approx( 3.5f ).epsilon( 0.01 ) );
    // Kinematic bodies follow their Transform.
    platform->GetComponent<Transform>().SetPosition( Vector3( -5.f, 4.f, 0.f ) );
    scene.Run( 0.1f );
    REQUIRE( scene.Physics.Raycast( Vector3( -5.f, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, platformHit ) );
    CHECK( platformHit.Position.y == doctest::Approx( 4.5f ).epsilon( 0.01 ) );

    // Moving a dynamic body's Transform from code teleports it.
    box->GetComponent<Transform>().SetPosition( Vector3( 10.f, 8.f, 0.f ) );
    scene.Run( kStep );
    CHECK( PositionOf( box ).x == doctest::Approx( 10.f ) );
    CHECK( PositionOf( box ).y < 8.f );
    CHECK( PositionOf( box ).y > 7.5f );
}

TEST_CASE( "Physics: queries hit the right entities and respect layer masks" )
{
    Scene scene;
    EntityHandle ground = scene.Ground();
    EntityHandle near = scene.Create( "Near", Vector3( 0.f, 2.f, 0.f ) );
    near->AddComponent<SphereCollider>().Radius = 0.5f;
    near->SetLayer( 3 );
    EntityHandle far = scene.Create( "Far", Vector3( 0.f, 5.f, 0.f ) );
    far->AddComponent<SphereCollider>().Radius = 0.5f;
    scene.Start();
    scene.Run( kStep );

    RaycastHit hit;
    REQUIRE( scene.Physics.Raycast( Vector3( 0.f, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 100.f, hit ) );
    CHECK( hit.Entity == far );
    CHECK( hit.Distance == doctest::Approx( 4.5f ) );
    CHECK( hit.Normal.y == doctest::Approx( 1.f ) );

    const std::vector<RaycastHit> all = scene.Physics.RaycastAll( Vector3( 0.f, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 100.f );
    REQUIRE( all.size() == 3 );
    CHECK( all[0].Entity == far );
    CHECK( all[1].Entity == near );
    CHECK( all[2].Entity == ground );

    // Masks: skip layer 0 (ground, far) and only see layer 3.
    REQUIRE( scene.Physics.Raycast( Vector3( 0.f, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 100.f, hit, PhysicsLayers::Bit( 3 ) ) );
    CHECK( hit.Entity == near );
    CHECK_FALSE( scene.Physics.Linecast( Vector3( 0.f, 10.f, 0.f ), Vector3( 0.f, 6.f, 0.f ), hit ) );
    CHECK( scene.Physics.Linecast( Vector3( 0.f, 10.f, 0.f ), Vector3( 0.f, 0.f, 0.f ), hit ) );

    // Sphere casts hit what a thin ray would miss.
    CHECK_FALSE( scene.Physics.Raycast( Vector3( 0.9f, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 7.f, hit ) );
    REQUIRE( scene.Physics.SphereCast( Vector3( 0.9f, 10.f, 0.f ), 0.5f, Vector3( 0.f, -1.f, 0.f ), 7.f, hit ) );
    CHECK( hit.Entity == far );

    const std::vector<EntityHandle> overlaps = scene.Physics.OverlapSphere( Vector3( 0.f, 2.f, 0.f ), 1.f );
    CHECK( overlaps.size() == 1 );
    CHECK( scene.Physics.OverlapBox( Vector3( 0.f, 3.5f, 0.f ), Vector3( 0.2f, 2.f, 0.2f ), Quaternion() ).size() == 2 );
    CHECK( scene.Physics.OverlapBox( Vector3( 0.f, 3.5f, 0.f ), Vector3( 0.2f, 2.f, 0.2f ), Quaternion(), PhysicsLayers::Bit( 0 ) ).size() == 1 );
}

TEST_CASE( "Physics: the layer collision matrix lets bodies pass through each other" )
{
    ProjectSettings& settings = ProjectSettings::Get();
    REQUIRE( settings.DoLayersCollide( 1, 2 ) );
    settings.SetLayersCollide( 1, 2, false );
    CHECK_FALSE( settings.DoLayersCollide( 2, 1 ) );   // symmetric
    CHECK( settings.DoLayersCollide( 1, 1 ) );

    Scene scene;
    EntityHandle ground = scene.Ground();
    ground->SetLayer( 1 );
    EntityHandle ghost = scene.DynamicBox( "Ghost", Vector3( 2.f, 3.f, 0.f ) );
    ghost->SetLayer( 2 );
    EntityHandle solid = scene.DynamicBox( "Solid", Vector3( -2.f, 3.f, 0.f ) );
    scene.Start();
    scene.Run( 2.f );
    CHECK( PositionOf( solid ).y == doctest::Approx( 0.5f ).epsilon( 0.02 ) );
    CHECK( PositionOf( ghost ).y < -5.f );

    settings.SetLayersCollide( 1, 2, true );
    CHECK( settings.DoLayersCollide( 1, 2 ) );
}

TEST_CASE( "Physics: triggers report enter and exit without blocking" )
{
    CollisionLog log;
    Scene scene;
    EntityHandle zone = scene.Create( "Zone", Vector3( 0.f, 0.f, 0.f ) );
    BoxCollider& trigger = zone->AddComponent<BoxCollider>();
    trigger.Size = Vector3( 4.f, 1.f, 4.f );
    trigger.IsTrigger = true;
    EntityHandle ball = scene.Create( "Ball", Vector3( 0.f, 3.f, 0.f ) );
    ball->AddComponent<Rigidbody>();
    ball->AddComponent<SphereCollider>().Radius = 0.25f;
    scene.Start();
    scene.Run( 2.f );

    CHECK( PositionOf( ball ).y < -3.f );   // fell straight through
    REQUIRE( log.Count( CollisionEvent::Phase::Enter, true ) == 1 );
    // Queries skip triggers.
    RaycastHit hit;
    CHECK_FALSE( scene.Physics.Raycast( Vector3( 1.5f, 5.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 10.f, hit ) );
    CHECK( scene.Physics.RaycastAll( Vector3( 1.5f, 5.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 10.f ).empty() );
    CHECK_FALSE( scene.Physics.SphereCast( Vector3( 1.5f, 5.f, 0.f ), 0.2f, Vector3( 0.f, -1.f, 0.f ), 10.f, hit ) );
    CHECK( scene.Physics.OverlapSphere( Vector3( 1.5f, 0.f, 0.f ), 0.3f ).empty() );
    CHECK( scene.Physics.OverlapBox( Vector3( 1.5f, 0.f, 0.f ), Vector3( 0.3f, 0.3f, 0.3f ), Quaternion() ).empty() );
    CHECK( log.Count( CollisionEvent::Phase::Exit, true ) == 1 );
    CHECK( log.Count( CollisionEvent::Phase::Enter, false ) == 0 );
    for( const CollisionEvent& event : log.Events )
    {
        CHECK( event.A == zone );
        CHECK( event.B == ball );
    }
}

TEST_CASE( "Physics: child colliders compound into the parent's body" )
{
    Scene scene;
    scene.Ground();
    EntityHandle dumbbell = scene.Create( "Dumbbell", Vector3( 0.f, 4.f, 0.f ) );
    dumbbell->AddComponent<Rigidbody>().Mass = 2.f;
    for( float side : { -1.f, 1.f } )
    {
        EntityHandle weight = scene.Create( "Weight", Vector3( side * 1.5f, 0.f, 0.f ) );
        weight->GetComponent<Transform>().SetParent( dumbbell->GetComponent<Transform>() );
        weight->AddComponent<SphereCollider>().Radius = 0.5f;
    }
    scene.Start();
    scene.Run( kStep );
    CHECK( scene.Physics.GetBodyCount() == 2 );   // ground + one compound body
    CHECK( dumbbell->GetComponent<Rigidbody>().GetBodyMass() == doctest::Approx( 2.f ) );

    scene.Run( 3.f );
    // Rests on both spheres: centre one radius above the floor, level.
    CHECK( PositionOf( dumbbell ).y == doctest::Approx( 0.5f ).epsilon( 0.03 ) );
    CHECK( std::abs( dumbbell->GetComponent<Transform>().GetWorldRotation().z ) < 0.02f );
}

TEST_CASE( "Physics: joints hold bodies together and break past their break force" )
{
    Scene scene;
    // A pendulum: a 2 m rope from a ball to the world point (0, 5, 0).
    EntityHandle bob = scene.Create( "Bob", Vector3( 2.f, 5.f, 0.f ) );
    bob->AddComponent<Rigidbody>().Mass = 1.f;
    bob->AddComponent<SphereCollider>().Radius = 0.2f;
    PhysicsJoint& rope = bob->AddComponent<PhysicsJoint>();
    rope.Type = JointType::Distance;
    rope.ConnectedAnchor = Vector3( 0.f, 5.f, 0.f );

    // A hinged door: rotates about its hinge axis only.
    EntityHandle door = scene.Create( "Door", Vector3( 10.f, 2.f, 0.f ) );
    Rigidbody& doorBody = door->AddComponent<Rigidbody>();
    doorBody.GravityScale = 0.f;
    door->AddComponent<BoxCollider>().Size = Vector3( 2.f, 3.f, 0.1f );
    PhysicsJoint& hinge = door->AddComponent<PhysicsJoint>();
    hinge.Type = JointType::Hinge;
    hinge.Anchor = Vector3( -1.f, 0.f, 0.f );
    hinge.Axis = Vector3( 0.f, 1.f, 0.f );

    scene.Start();
    scene.Run( kStep );
    doorBody.SetAngularVelocity( Vector3( 0.f, 2.f, 0.f ) );
    scene.Run( 0.8f );   // about a quarter swing

    const Vector3 toBob = PositionOf( bob ) - Vector3( 0.f, 5.f, 0.f );
    CHECK( toBob.Length() == doctest::Approx( 2.f ).epsilon( 0.03 ) );
    CHECK( PositionOf( bob ).y < 3.6f );

    const Vector3 hingePoint = door->GetComponent<Transform>().GetLocalToWorldMatrix().TransformPoint( Vector3( -1.f, 0.f, 0.f ) );
    CHECK( hingePoint.x == doctest::Approx( 9.f ).epsilon( 0.01 ) );
    CHECK( hingePoint.z == doctest::Approx( 0.f ).scale( 1.f ).epsilon( 0.01 ) );
    CHECK( std::abs( doorBody.GetAngularVelocity().x ) < 0.01f );
    // Spin about the centre, then pinned at an edge: I_c / (I_c + m r^2) of it survives (~0.5 rad/s).
    CHECK( std::abs( doorBody.GetAngularVelocity().y ) == doctest::Approx( 0.5f ).epsilon( 0.1 ) );

    // A weak fixed joint snaps under a heavy load and the body falls.
    EntityHandle weight = scene.Create( "Weight", Vector3( -10.f, 5.f, 0.f ) );
    weight->AddComponent<Rigidbody>().Mass = 50.f;
    weight->AddComponent<BoxCollider>();
    PhysicsJoint& weld = weight->AddComponent<PhysicsJoint>();
    weld.Type = JointType::Fixed;
    weld.BreakForce = 10.f;
    scene.Run( 1.f );
    CHECK( PositionOf( weight ).y < 2.f );
}

TEST_CASE( "Physics: the character controller walks, is stopped by walls, and jumps" )
{
    Scene scene;
    scene.Ground();
    EntityHandle wall = scene.Create( "Wall", Vector3( 4.f, 1.f, 0.f ) );
    wall->AddComponent<BoxCollider>().Size = Vector3( 1.f, 2.f, 10.f );
    EntityHandle player = scene.Create( "Player", Vector3( 0.f, 0.95f, 0.f ) );
    CharacterController& controller = player->AddComponent<CharacterController>();
    scene.Start();

    scene.Run( 0.25f );
    CHECK( controller.IsOnGround() );
    CHECK( PositionOf( player ).y == doctest::Approx( 0.9f ).epsilon( 0.02 ) );

    controller.SetMoveInput( Vector3( 1.f, 0.f, 0.f ) );
    scene.Run( 0.25f );
    CHECK( PositionOf( player ).x > 0.5f );
    scene.Run( 2.f );
    // Wall face at x = 3.5, capsule radius 0.4.
    CHECK( PositionOf( player ).x == doctest::Approx( 3.1f ).epsilon( 0.02 ) );
    CHECK( controller.IsOnGround() );

    controller.SetMoveInput( Vector3() );
    controller.Jump();
    scene.Run( 0.25f );
    CHECK_FALSE( controller.IsOnGround() );
    CHECK( PositionOf( player ).y > 1.5f );
    scene.Run( 1.5f );
    CHECK( controller.IsOnGround() );
    CHECK( PositionOf( player ).y == doctest::Approx( 0.9f ).epsilon( 0.02 ) );

    // The capsule is in the world for queries and rigidbodies.
    RaycastHit hit;
    REQUIRE( scene.Physics.Raycast( Vector3( PositionOf( player ).x, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, hit ) );
    CHECK( hit.Entity == player );
}

TEST_CASE( "Physics: Bullet-era rigidbodies migrate to a Rigidbody plus a collider" )
{
    const json entity = {
        { "Components", json::array( {
            { { "Type", "Transform" }, { "Scale", { 40.0, 1.0, 40.0 } } },
            { { "Type", "Rigidbody" }, { "ColliderType", "Box" }, { "Mass", 0.0 }, { "Scale", { 40.0, 1.0, 40.0 } } },
        } ) },
    };
    std::vector<json> floor = SceneSerializer::UpgradeComponent( entity["Components"][1], entity );
    REQUIRE( floor.size() == 1 );   // static: just a collider
    CHECK( floor[0]["Type"] == "BoxCollider" );
    CHECK( floor[0]["Size"][0].get<float>() == doctest::Approx( 2.f ) );
    CHECK( floor[0]["Size"][1].get<float>() == doctest::Approx( 2.f ) );

    const json crate = { { "Type", "Rigidbody" }, { "ColliderType", "Sphere" }, { "Mass", 5.0 } };
    std::vector<json> ball = SceneSerializer::UpgradeComponent( crate, json::object() );
    REQUIRE( ball.size() == 2 );
    CHECK( ball[0]["Type"] == "Rigidbody" );
    CHECK( ball[0]["BodyType"] == "Dynamic" );
    CHECK( ball[0]["Mass"].get<float>() == doctest::Approx( 5.f ) );
    CHECK( ball[1]["Type"] == "SphereCollider" );

    // Current-format components pass through untouched.
    const json current = { { "Type", "Rigidbody" }, { "BodyType", "Kinematic" } };
    std::vector<json> same = SceneSerializer::UpgradeComponent( current, json::object() );
    REQUIRE( same.size() == 1 );
    CHECK( same[0] == current );
}

TEST_CASE( "Physics: compound colliders follow a rotated parent" )
{
    Scene scene;
    // Parent turned 90 degrees about Z: its local +X points along world +Y.
    EntityHandle arm = scene.Create( "Arm", Vector3( 0.f, 3.f, 0.f ) );
    arm->GetComponent<Transform>().SetRotation( Quaternion( 0.f, 0.f, std::sin( 0.785398f ), std::cos( 0.785398f ) ) );
    arm->AddComponent<Rigidbody>().Type = BodyType::Kinematic;
    EntityHandle tip = scene.Create( "Tip", Vector3( 2.f, 0.f, 0.f ) );
    tip->GetComponent<Transform>().SetParent( arm->GetComponent<Transform>() );
    tip->AddComponent<SphereCollider>().Radius = 0.5f;
    scene.Start();
    scene.Run( kStep );

    RaycastHit hit;
    REQUIRE( scene.Physics.Raycast( Vector3( 0.f, 10.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, hit ) );
    CHECK( hit.Entity == tip );
    CHECK( hit.Position.y == doctest::Approx( 5.5f ).epsilon( 0.001 ) );
}

TEST_CASE( "Physics: joint motors and sliders act on the joint's own entity" )
{
    Scene scene;
    // A turntable spinning +90 deg/s about +Y (right-handed), driven against the world.
    EntityHandle table = scene.Create( "Table", Vector3( 0.f, 2.f, 0.f ) );
    Rigidbody& tableBody = table->AddComponent<Rigidbody>();
    tableBody.GravityScale = 0.f;
    table->AddComponent<BoxCollider>().Size = Vector3( 2.f, 0.2f, 0.5f );
    PhysicsJoint& hinge = table->AddComponent<PhysicsJoint>();
    hinge.Type = JointType::Hinge;
    hinge.Axis = Vector3( 0.f, 1.f, 0.f );
    hinge.UseMotor = true;
    hinge.MotorSpeed = 90.f;
    hinge.MaxMotorForce = 1000.f;

    // A slider along +X pushed by its motor towards the upper limit.
    EntityHandle piston = scene.Create( "Piston", Vector3( 10.f, 2.f, 0.f ) );
    piston->AddComponent<Rigidbody>().GravityScale = 0.f;
    piston->AddComponent<BoxCollider>();
    PhysicsJoint& slider = piston->AddComponent<PhysicsJoint>();
    slider.Type = JointType::Slider;
    slider.Axis = Vector3( 1.f, 0.f, 0.f );
    slider.UseLimits = true;
    slider.LowerLimit = -1.f;
    slider.UpperLimit = 2.f;
    slider.UseMotor = true;
    slider.MotorSpeed = 1.f;
    slider.MaxMotorForce = 1000.f;

    scene.Start();
    scene.Run( 1.f );
    CHECK( tableBody.GetAngularVelocity().y == doctest::Approx( 3.14159265f * 0.5f ).epsilon( 0.02 ) );
    const Vector3 front = table->GetComponent<Transform>().GetWorldRotation() * Vector3( 1.f, 0.f, 0.f );
    CHECK( front.z == doctest::Approx( -1.f ).epsilon( 0.05 ) );   // +X turned 90 degrees about +Y
    CHECK( PositionOf( piston ).x == doctest::Approx( 11.f ).epsilon( 0.01 ) );
    scene.Run( 2.f );
    CHECK( PositionOf( piston ).x == doctest::Approx( 12.f ).epsilon( 0.01 ) );   // stopped at the upper limit
}


TEST_CASE( "Physics: audio occlusion counts the objects between the listener and a source" )
{
    Scene scene;
    EntityHandle listener = scene.Create( "Listener", Vector3( 0.f, 1.f, 0.f ) );
    listener->AddComponent<SphereCollider>().Radius = 0.5f;     // the player around the listener
    EntityHandle speaker = scene.Create( "Speaker", Vector3( 0.f, 1.f, 10.f ) );
    speaker->AddComponent<BoxCollider>().Size = Vector3( 1.f, 1.f, 1.f );   // its own housing
    EntityHandle wallA = scene.Create( "Wall A", Vector3( 0.f, 1.f, 3.f ) );
    wallA->AddComponent<BoxCollider>().Size = Vector3( 4.f, 4.f, 0.5f );
    EntityHandle wallB = scene.Create( "Wall B", Vector3( 0.f, 1.f, 6.f ) );
    wallB->AddComponent<BoxCollider>().Size = Vector3( 4.f, 4.f, 0.5f );
    EntityHandle panel = scene.Create( "Wall B Panel", Vector3( 0.f, 1.f, 6.6f ) );
    panel->GetComponent<Transform>().SetParent( wallB->GetComponent<Transform>(), true );
    panel->AddComponent<BoxCollider>().Size = Vector3( 4.f, 4.f, 0.5f );   // same object: counts once
    EntityHandle zone = scene.Create( "Zone", Vector3( 0.f, 1.f, 8.f ) );
    BoxCollider& trigger = zone->AddComponent<BoxCollider>();
    trigger.Size = Vector3( 4.f, 4.f, 1.f );
    trigger.IsTrigger = true;                                     // triggers never block
    scene.Start();
    scene.Run( kStep );

    const Vector3 ear( 0.f, 1.f, 0.f );
    CHECK( CountAudioObstacles( scene.Physics, ear, PositionOf( speaker ), listener.Get(), *speaker.Get() ) == 2 );
    CHECK( CountAudioObstacles( scene.Physics, ear, Vector3( 0.f, 1.f, 4.5f ), listener.Get(), *speaker.Get() ) == 1 );
    CHECK( CountAudioObstacles( scene.Physics, ear, Vector3( 0.f, 1.f, 2.f ), listener.Get(), *speaker.Get() ) == 0 );
    CHECK( CountAudioObstacles( scene.Physics, ear, Vector3( 8.f, 1.f, 0.f ), listener.Get(), *speaker.Get() ) == 0 );
    // Without the listener's entity its own collider would count.
    CHECK( CountAudioObstacles( scene.Physics, Vector3( 0.f, 1.f, -1.f ), PositionOf( speaker ), nullptr, *speaker.Get() ) == 3 );
}
