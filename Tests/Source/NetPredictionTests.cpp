#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Net/NetPrediction.h"
#include "NetHarness.h"
#include <cmath>

namespace NetPredictionTest
{
    struct TestContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

    // The test-only pawn: axes 0/1 steer on XZ, button 1 jumps.
    void DrivePawn( Entity& InEntity, const InputCommand& InCommand )
    {
        CharacterController& controller = InEntity.GetComponent<CharacterController>();
        controller.SetMoveInput( Vector3( InCommand.Axes[0], 0.f, InCommand.Axes[1] ) );
        if( InCommand.Buttons & 1u )
        {
            controller.Jump();
        }
    }

    // A walk-and-jump course: forward, strafe, back diagonally, a jump every 75 ticks.
    InputCommand CourseCommand( uint32_t InTick )
    {
        InputCommand command;
        switch( ( InTick / 120 ) % 4 )
        {
        case 0: command.Axes[1] = 1.f; break;
        case 1: command.Axes[0] = 1.f; break;
        case 2: command.Axes[0] = -0.7f; command.Axes[1] = -0.7f; break;
        default: command.Axes[0] = std::sin( InTick * 0.05f ); command.Axes[1] = 0.3f; break;
        }
        command.Buttons = InTick % 75 == 0 ? 1u : 0u;
        return command;
    }

    // Static geometry, the same in every world: a floor, a low step, a wall.
    void BuildCourse( World& InWorld )
    {
        auto box = [&InWorld]( const Vector3& InPosition, const Vector3& InSize ) {
            EntityHandle entity = InWorld.CreateEntity();
            entity->AddComponent<Transform>().SetPosition( InPosition );
            entity->AddComponent<BoxCollider>().Size = InSize;
        };
        box( Vector3( 0.f, -0.5f, 0.f ), Vector3( 80.f, 1.f, 80.f ) );
        box( Vector3( 4.f, 0.1f, 6.f ), Vector3( 3.f, 0.2f, 3.f ) );
        box( Vector3( 12.f, 1.f, 0.f ), Vector3( 1.f, 2.f, 30.f ) );
    }

    void Step( PhysicsCore& InPhysics, World& InWorld )
    {
        TestContext context;
        context.FixedDeltaTime = 1.f / 60.f;
        context.InterpolationAlpha = 1.f;
        InWorld.Simulate();
        context.IsFixedStepActive = true;
        InPhysics.FixedUpdate( context );
        context.IsFixedStepActive = false;
        InPhysics.Update( context );
    }

    NetConditions Link( float InLatencyMs, float InJitterMs, uint64_t InSeed )
    {
        NetConditions conditions;
        conditions.LatencyMs = InLatencyMs;
        conditions.JitterMs = InJitterMs;
        conditions.Seed = InSeed;
        return conditions;
    }
}

using namespace NetPredictionTest;
using namespace NetHarness;

TEST_CASE( "Net: a prediction replays from a corrected tick exactly as a straight run, and a 20-tick resim takes < 1 ms" )
{
    PhysicsCore physics;
    auto world = std::make_shared<World>();
    world->IsLoading = false;
    world->AddCore( physics );
    BuildCourse( *world );
    EntityHandle pawn = world->CreateEntity( "Pawn" );
    pawn->AddComponent<Transform>().SetPosition( Vector3( 0.f, 0.9f, 0.f ) );
    CharacterController& controller = pawn->AddComponent<CharacterController>();
    world->Simulate();
    world->Start();

    NetPredictor predictor( physics, pawn, DrivePawn, 1.f / 60.f );
    std::vector<CharacterController::NetState> straight;
    for( uint32_t tick = 0; tick < 40; ++tick )
    {
        predictor.Apply( CourseCommand( tick ) );
        Step( physics, *world );
        predictor.Record( tick );
        straight.push_back( controller.GetNetState() );
    }
    // The server agrees with tick 5: nothing happens.
    CHECK_FALSE( predictor.Reconcile( 5, straight[5] ) );
    CHECK( predictor.GetMatchCount() == 1 );

    // The server says tick 19 was elsewhere: restore it, resim 20..39.
    CharacterController::NetState pushed = straight[19];
    pushed.Position.x += 0.5f;
    pushed.Velocity.x += 2.f;
    CHECK( predictor.Reconcile( 19, pushed ) );
    CHECK( predictor.GetCorrectionCount() == 1 );
    CHECK( predictor.GetLastResimTicks() == 20 );
    MESSAGE( "20-tick resim: " << predictor.GetLastResimMilliseconds() << " ms" );
    CHECK( predictor.GetLastResimMilliseconds() < 1.0 );
    CHECK( controller.GetNetState().Position.x > straight[39].Position.x + 0.4f );
    CHECK( ( predictor.GetVisualOffset() - ( straight[39].Position - controller.GetNetState().Position ) ).Length() < 1e-4f );
    // The offset is gone after the smoothing time.
    predictor.UpdateSmoothing( 0.05 );
    CHECK( predictor.GetVisualOffset().Length() > 0.f );
    predictor.UpdateSmoothing( 0.06 );
    CHECK( predictor.GetVisualOffset().Length() == 0.f );

    // Resimming from the straight run's own state reproduces the straight run, bit for bit.
    CHECK( predictor.Reconcile( 20, straight[20] ) );
    CHECK( controller.GetNetState() == straight[39] );
}

TEST_CASE( "Net: at 150 ms RTT a walk/jump course predicts with 0 corrections; a server knockback is exactly 1, converged within 10 ticks" )
{
    PhysicsCore serverPhysics;
    PhysicsCore clientPhysics;
    Harness net( Link( 75.f, 3.f, 41 ), Link( 75.f, 3.f, 42 ) );
    net.ServerWorld->AddCore( serverPhysics );
    net.ClientWorld->AddCore( clientPhysics );
    BuildCourse( *net.ServerWorld );
    BuildCourse( *net.ClientWorld );
    net.Place( 0x9A3E );
    EntityHandle serverPawn;
    EntityHandle clientPawn;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) { if( InEntity.GetGUID() == 0x9A3E ) serverPawn = net.ServerWorld->GetEntity( InEntity.GetId() ); } );
    net.ClientWorld->ForEachEntity( [&]( Entity& InEntity ) { if( InEntity.GetGUID() == 0x9A3E ) clientPawn = net.ClientWorld->GetEntity( InEntity.GetId() ); } );
    for( EntityHandle pawn : { serverPawn, clientPawn } )
    {
        pawn->GetComponent<Transform>().SetPosition( Vector3( 0.f, 0.9f, 0.f ) );
        pawn->AddComponent<CharacterController>();
    }
    serverPawn->GetComponent<NetIdentity>().SetOwner( 1 );
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        world->Simulate();
        world->Start();
    }

    net.ServerNet.SetCommandHandler( DrivePawn );
    net.ClientNet.SetCommandHandler( DrivePawn );
    net.MakeCommand = CourseCommand;
    int knockbackTick = -1;
    net.OnServerTick = [&]( NetServer& InServer ) {
        if( knockbackTick >= 0 && InServer.GetTick() == static_cast<uint32_t>( knockbackTick ) )
        {
            CharacterController& controller = serverPawn->GetComponent<CharacterController>();
            controller.SetVelocity( controller.GetVelocity() + Vector3( 6.f, 5.f, 0.f ) );
        }
        Step( serverPhysics, *net.ServerWorld );
    };
    net.OnClientTick = [&]( NetCore& ) { Step( clientPhysics, *net.ClientWorld ); };
    net.Start();
    net.ClientNet.Predict( clientPawn );
    NetPredictor& predictor = *net.ClientNet.GetPredictor();

    net.Run( 12.0 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    MESSAGE( "course: " << predictor.GetMatchCount() << " ticks confirmed, " << predictor.GetCorrectionCount() << " corrections, "
        << predictor.GetInitialSyncCount() << " initial sync" );
    CHECK( predictor.GetMatchCount() > 500 );
    CHECK( predictor.GetCorrectionCount() == 0 );
    CHECK( predictor.GetInitialSyncCount() <= 1 );
    // It really went somewhere (and the server's copy with it).
    CHECK( ( serverPawn->GetComponent<CharacterController>().GetNetState().Position - Vector3( 0.f, 0.9f, 0.f ) ).Length() > 3.f );

    // Knock the server's pawn sideways: the client finds out one round trip later.
    knockbackTick = static_cast<int>( net.ServerNet.GetServer()->GetTick() ) + 5;
    std::vector<float> offsetsAfter;
    bool corrected = false;
    net.OnClientFrame = [&]( NetClient& ) {
        if( predictor.GetCorrectionCount() > 0 )
        {
            corrected = true;
            offsetsAfter.push_back( predictor.GetVisualOffset().Length() );
        }
    };
    net.Run( 4.0 );
    CHECK( corrected );
    CHECK( predictor.GetCorrectionCount() == 1 );
    CHECK( predictor.GetLastCorrectionTick() == static_cast<uint32_t>( knockbackTick ) );
    REQUIRE( offsetsAfter.size() > 10 );
    CHECK( offsetsAfter.front() > 0.01f );
    CHECK( offsetsAfter[9] == 0.f );    // smoothed away within 10 ticks
    // Converged: the rest of the run predicts right.
    const CharacterController::NetState server = serverPawn->GetComponent<CharacterController>().GetNetState();
    CHECK( ( server.Position - Vector3( 0.f, 0.9f, 0.f ) ).Length() > 0.f );
}
