#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Engine/NetDebuggerView.h"
#include "Net/NetPrediction.h"
#include "NetHarness.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>

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
    // The misprediction timeline (NET-7): 5 matched, 19 was corrected, nothing else was reconciled.
    CHECK( predictor.GetOutcome( 5 ) == NetPredictOutcome::Match );
    CHECK( predictor.GetOutcome( 19 ) == NetPredictOutcome::Correction );
    CHECK( predictor.GetOutcome( 6 ) == NetPredictOutcome::None );
    CHECK( predictor.GetTimelineTicks() == 600 );
    REQUIRE( predictor.GetMispredicts().size() == 1 );
    {
        const NetMispredictRecord& record = predictor.GetMispredicts().front();
        CHECK( predictor.FindMispredict( 19 ) == &record );
        CHECK_FALSE( record.InitialSync );
        CHECK( record.Predicted == straight[19] );
        CHECK( record.Authoritative == pushed );
        CHECK( record.PositionDelta().x == doctest::Approx( 0.5f ) );
        CHECK( record.VelocityDelta().x == doctest::Approx( 2.f ) );
        CHECK( record.HasStart );
        CHECK( record.Start == straight[18] );
        CHECK( record.Commands.size() == 21 );     // 19's, then 20..39's
        CHECK( record.Commands.front().Tick == 19 );
        CHECK( record.Commands.back().Tick == 39 );
        CHECK( record.PreviousCommand.Tick == 18 );
        REQUIRE( record.Resimulated.size() == 20 );
        CHECK( record.Resimulated.back() == controller.GetNetState() );
        CHECK( record.NewestTick == 39 );
        CHECK( ( record.PresentJump - ( controller.GetNetState().Position - straight[39].Position ) ).Length() < 1e-6f );
        MESSAGE( "tick 19: " << record.Describe() );
        CHECK( record.Describe().find( "position off 500.0 mm" ) == 0 );
    }
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
    std::map<uint32_t, CharacterController::NetState> serverStates;   // after each tick
    net.OnServerTick = [&]( NetServer& InServer ) {
        if( knockbackTick >= 0 && InServer.GetTick() == static_cast<uint32_t>( knockbackTick ) )
        {
            CharacterController& controller = serverPawn->GetComponent<CharacterController>();
            controller.SetVelocity( controller.GetVelocity() + Vector3( 6.f, 5.f, 0.f ) );
        }
        Step( serverPhysics, *net.ServerWorld );
        serverStates[InServer.GetTick()] = serverPawn->GetComponent<CharacterController>().GetNetState();
    };
    std::map<uint32_t, CharacterController::NetState> clientStates;   // after each predicted tick
    net.OnClientTick = [&]( NetCore& InNet ) {
        Step( clientPhysics, *net.ClientWorld );
        clientStates[InNet.GetTick()] = clientPawn->GetComponent<CharacterController>().GetNetState();
    };
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
    // Each frame as the engine presents it (NET-7): the simulated pose, then the pawn as drawn
    // (BeginPresentation, after gameplay), then the simulated pose back (EndPresentation).
    struct Shown
    {
        Vector3 Simulated;
        Vector3 Drawn;
        bool Restored = false;      // EndPresentation put the simulated pose back to the bit
    };
    std::vector<Shown> shown;
    size_t correctionFrame = 0;
    uint32_t correctedTick = 0;     // the pawn's newest tick on that frame, and its state
    CharacterController::NetState correctedState;
    Transform& clientTransform = clientPawn->GetComponent<Transform>();
    net.OnClientFrame = [&]( NetClient& ) {
        Shown frame;
        frame.Simulated = clientTransform.GetWorldPosition();
        net.ClientNet.BeginPresentation();
        frame.Drawn = clientTransform.GetWorldPosition();
        net.ClientNet.EndPresentation();
        const Vector3 after = clientTransform.GetWorldPosition();
        frame.Restored = after.x == frame.Simulated.x && after.y == frame.Simulated.y && after.z == frame.Simulated.z;
        shown.push_back( frame );
        if( predictor.GetCorrectionCount() > 0 )
        {
            if( !corrected )
            {
                correctionFrame = shown.size() - 1;
                correctedTick = clientStates.rbegin()->first;
                correctedState = clientPawn->GetComponent<CharacterController>().GetNetState();
            }
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
    // On the timeline: the knockback tick red, the ticks either side of it green, one record.
    CHECK( predictor.GetOutcome( static_cast<uint32_t>( knockbackTick ) ) == NetPredictOutcome::Correction );
    CHECK( predictor.GetOutcome( static_cast<uint32_t>( knockbackTick - 1 ) ) == NetPredictOutcome::Match );
    CHECK( predictor.GetOutcome( static_cast<uint32_t>( knockbackTick + 1 ) ) == NetPredictOutcome::Match );
    REQUIRE( predictor.GetMispredicts().size() == 1 );     // the initial sync was 16 s ago: off the timeline
    const NetMispredictRecord& knocked = predictor.GetMispredicts().front();
    CHECK( knocked.Tick == static_cast<uint32_t>( knockbackTick ) );
    CHECK_FALSE( knocked.InitialSync );
    CHECK( knocked.PositionDelta().Length() > 0.f );
    CHECK( knocked.VelocityDelta().x == doctest::Approx( 6.f ).epsilon( 0.2 ) );
    CHECK( knocked.HasStart );
    CHECK( knocked.Commands.size() == knocked.Resimulated.size() + 1 );
    CHECK( knocked.Resimulated.size() > 5 );   // a round trip of ticks at 150 ms
    MESSAGE( "knockback at tick " << knocked.Tick << ": " << knocked.Describe() << "; " << knocked.Resimulated.size() << " ticks resimulated" );
    CHECK( knocked.Authoritative == serverStates[knocked.Tick] );

    // The Net Debugger's view (NetDebuggerView): what the Player's window and Havana's panel both
    // draw, and the same after the trip through JSON a PIE child's "net view" answer takes.
    {
        NetRateMeter rate;
        const NetDebuggerView view = NetDebuggerView::Capture( net.ClientNet, rate );
        CHECK( view.Client );
        CHECK( view.Connected );
        CHECK( view.RoundTripMs > 100.f );
        CHECK( view.Predicting );
        CHECK( view.Corrections == 1 );
        REQUIRE( view.HasTimeline );
        CHECK( view.Outcomes.size() == view.Newest - view.First + 1 );
        CHECK( view.GetOutcome( static_cast<uint32_t>( knockbackTick ) ) == NetPredictOutcome::Correction );
        CHECK( view.GetOutcome( static_cast<uint32_t>( knockbackTick + 1 ) ) == NetPredictOutcome::Match );
        CHECK( view.CountOnTimeline( NetPredictOutcome::Correction ) == 1 );
        REQUIRE( view.Marks.size() == 1 );
        CHECK( view.Marks[0].Tick == knocked.Tick );
        CHECK( view.Marks[0].Describe == knocked.Describe() );
        REQUIRE( view.HasDetail );
        CHECK( view.Detail.Tick == knocked.Tick );
        CHECK( view.Detail.Authoritative == knocked.Authoritative );
        CHECK( view.Detail.Commands == knocked.Commands );
        NetDebuggerView loaded;
        std::string error;
        REQUIRE_MESSAGE( NetDebuggerView::FromJson( json::parse( view.ToJson().dump() ), loaded, &error ), error );
        CHECK( loaded.ToJson() == view.ToJson() );
        CHECK( loaded.RoundTripMs == view.RoundTripMs );
        CHECK( loaded.Outcomes == view.Outcomes );
        CHECK( loaded.Detail.Predicted == view.Detail.Predicted );
        CHECK( loaded.Detail.Commands == view.Detail.Commands );
        CHECK( loaded.Detail.Resimulated.size() == view.Detail.Resimulated.size() );
        // A cut or hand-edited answer is an error, not a throw.
        json cut = view.ToJson();
        cut["prediction"]["timeline"]["outcomes"] = "12";
        CHECK_FALSE( NetDebuggerView::FromJson( cut, loaded, &error ) );
        CHECK( error.find( "timeline" ) != std::string::npos );
        json wrong = view.ToJson();
        wrong["link"]["rtt"] = "fast";
        CHECK_FALSE( NetDebuggerView::FromJson( wrong, loaded, &error ) );
        CHECK_FALSE( NetDebuggerView::FromJson( json( 5 ), loaded, &error ) );
        CHECK_FALSE( loaded.Client );
        // A server session has nothing to show, and says so.
        const NetDebuggerView server = NetDebuggerView::Capture( net.ServerNet, rate );
        CHECK_FALSE( server.Client );
        CHECK_FALSE( server.Reason.empty() );
        // The graphs take a sample at most every quarter second.
        NetDebuggerHistory history;
        history.Add( view, 10.0 );
        history.Add( view, 10.1 );
        history.Add( view, 10.3 );
        CHECK( history.Samples.size() == 2 );
        CHECK( history.Samples.back().RoundTripMs == view.RoundTripMs );
    }

    // Presented (NET-7): the simulated pawn is the server's at once, the drawn one glides. On the
    // frame the correction came, the pawn's newest tick is exactly the server's state for that tick
    // and its Transform (outside the presentation) is that state; the drawn pawn never moves more
    // than 1/6 of the correction in a frame beyond the simulated pawn's own motion (100 ms of
    // smoothing at 60 frames a second), and the simulated pose comes back bit for bit every frame.
    REQUIRE( correctionFrame > 0 );
    CHECK( correctedState == serverStates[correctedTick] );
    CHECK( clientStates[correctedTick] == correctedState );
    CHECK( ( shown[correctionFrame].Simulated - correctedState.Position ).Length() == 0.f );
    CHECK( ( shown[correctionFrame].Drawn - shown[correctionFrame].Simulated ).Length() > 0.1f );
    const float jump = knocked.PresentJump.Length();
    float worstStep = 0.f;
    size_t restored = 0;
    for( size_t i = correctionFrame; i < shown.size(); ++i )
    {
        // The simulated pawn's own motion this frame: what it moved, less the correction's jump.
        const Vector3 motion = shown[i].Simulated - shown[i - 1].Simulated - ( i == correctionFrame ? knocked.PresentJump : Vector3() );
        worstStep = std::max( worstStep, ( shown[i].Drawn - shown[i - 1].Drawn - motion ).Length() );
    }
    for( const Shown& frame : shown )
    {
        restored += frame.Restored ? 1 : 0;
    }
    MESSAGE( "presented: a " << jump * 1000.f << " mm correction, the drawn pawn's biggest step beyond its motion " << worstStep * 1000.f << " mm (1/6: " << jump / 6.f * 1000.f
        << " mm)" );
    CHECK( jump > 0.1f );
    CHECK( worstStep <= jump / 6.f + 1e-4f );
    CHECK( worstStep > jump / 7.f );   // it did glide: in steps, not all at once nor not at all
    CHECK( restored == shown.size() );
    CHECK_FALSE( net.ClientNet.IsPresenting() );

    // Exported (on the background lane), read back, and run again offline in a world of its own
    // with the same level: the client's prediction for the tick comes out again bit for bit (the
    // desync, reproduced: it isn't the server's), and the resim from the server's state follows the
    // server's own ticks after it exactly (it took the same commands).
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "MitchMispredictTests";
    std::filesystem::remove_all( folder );
    const std::string path = NetMispredict::DefaultPath( folder.generic_string(), knocked.Tick );
    REQUIRE( NetMispredict::Export( knocked, path ).get() );
    NetMispredictRecord loaded;
    std::string error;
    REQUIRE_MESSAGE( NetMispredict::Load( path, loaded, &error ), error );
    CHECK( loaded.Tick == knocked.Tick );
    CHECK( loaded.Predicted == knocked.Predicted );
    CHECK( loaded.Authoritative == knocked.Authoritative );
    CHECK( loaded.Start == knocked.Start );
    CHECK( loaded.Commands == knocked.Commands );
    CHECK( loaded.Resimulated.size() == knocked.Resimulated.size() );
    CHECK( loaded.Describe() == knocked.Describe() );
    {
        PhysicsCore physics;
        auto world = std::make_shared<World>();
        world->IsLoading = false;
        world->AddCore( physics );
        BuildCourse( *world );
        EntityHandle pawn = world->CreateEntity( "Pawn" );
        pawn->AddComponent<Transform>().SetPosition( Vector3( 0.f, 0.9f, 0.f ) );
        pawn->AddComponent<CharacterController>();
        world->Simulate();
        world->Start();
        const NetMispredict::Repro repro = NetMispredict::Run( physics, *pawn.Get(), loaded, DrivePawn );
        REQUIRE_MESSAGE( repro.Ran(), repro.Error );
        CHECK( repro.PredictedMatches );
        CHECK_FALSE( NetPredictor::Matches( repro.Predicted, serverStates[loaded.Tick], 0.001f, 0.01f ) );
        CHECK( repro.ResimMatches == loaded.Resimulated.size() );
        CHECK( repro.Reproduced( loaded ) );
        size_t sameAsServer = 0;
        for( size_t i = 0; i < repro.Resimulated.size(); ++i )
        {
            sameAsServer += repro.Resimulated[i] == serverStates[loaded.Tick + 1 + static_cast<uint32_t>( i )] ? 1 : 0;
        }
        MESSAGE( "offline repro of tick " << loaded.Tick << ": the prediction again, then " << sameAsServer << " of " << repro.Resimulated.size()
            << " resimulated ticks the server's to the bit" );
        CHECK( sameAsServer == repro.Resimulated.size() );
    }
    std::filesystem::remove_all( folder );
    // Converged: the rest of the run predicts right.
    const CharacterController::NetState server = serverPawn->GetComponent<CharacterController>().GetNetState();
    CHECK( ( server.Position - Vector3( 0.f, 0.9f, 0.f ) ).Length() > 0.f );
}


TEST_CASE( "Net: a misprediction's differing script bytes are found as ranges" )
{
    using Ranges = std::vector<std::pair<uint32_t, uint32_t>>;
    CHECK( NetPredictor::DiffBytes( { 1, 2, 3 }, { 1, 2, 3 } ).empty() );
    CHECK( NetPredictor::DiffBytes( { 1, 2, 3, 4, 5 }, { 1, 9, 9, 4, 6 } ) == Ranges{ { 1, 3 }, { 4, 5 } } );
    CHECK( NetPredictor::DiffBytes( { 1, 2 }, { 1, 2, 3, 4 } ) == Ranges{ { 2, 4 } } );
    CHECK( NetPredictor::DiffBytes( { 1, 2, 3 }, { 1, 5 } ) == Ranges{ { 1, 3 } } );
}


TEST_CASE( "Net: a correction too far to glide (the server teleported the pawn) snaps instead: no visual offset" )
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
    REQUIRE( predictor.MaxSmoothDistance == 2.f );
    std::vector<CharacterController::NetState> straight;
    for( uint32_t tick = 0; tick < 40; ++tick )
    {
        predictor.Apply( CourseCommand( tick ) );
        Step( physics, *world );
        predictor.Record( tick );
        straight.push_back( controller.GetNetState() );
    }
    CHECK_FALSE( predictor.Reconcile( 5, straight[5] ) );

    // Pushed 20 cm: it glides.
    CharacterController::NetState pushed = straight[10];
    pushed.Position.x += 0.2f;
    CHECK( predictor.Reconcile( 10, pushed ) );
    CHECK( predictor.GetVisualOffset().Length() > 0.1f );
    CHECK( predictor.GetVisualOffset().Length() < predictor.MaxSmoothDistance );

    // Teleported 25 m (a respawn): drawn where it is at once, nothing to smooth.
    CharacterController::NetState teleported = straight[20];
    teleported.Position = Vector3( -25.f, 0.9f, 0.f );
    teleported.Velocity = Vector3();
    CHECK( predictor.Reconcile( 20, teleported ) );
    CHECK( predictor.GetCorrectionCount() == 2 );
    CHECK( controller.GetNetState().Position.x < -20.f );
    CHECK( predictor.GetVisualOffset().Length() == 0.f );
    predictor.UpdateSmoothing( 1.0 / 60.0 );
    CHECK( predictor.GetVisualOffset().Length() == 0.f );
}


TEST_CASE( "Net: mismatch records age off the timeline with matches alone, and a steady desync keeps the newest 64 while every tick stays a correction" )
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
    const uint32_t span = predictor.GetTimelineTicks();
    REQUIRE( span == 600 );
    // Each tick reconciled as soon as it's recorded: 0..9 match, 10..79 are pushed 5 cm (a steady
    // desync, 70 corrections), then only matches.
    for( uint32_t tick = 0; tick < 80 + span + 10; ++tick )
    {
        predictor.Apply( CourseCommand( tick ) );
        Step( physics, *world );
        predictor.Record( tick );
        CharacterController::NetState server = controller.GetNetState();
        const bool desync = tick >= 10 && tick < 80;
        if( desync )
        {
            server.Position.x += 0.05f;
        }
        CHECK( predictor.Reconcile( tick, server ) == desync );
        if( tick == 79 )
        {
            CHECK( predictor.GetCorrectionCount() == 70 );
            // Only the newest kMaxMispredicts keep a record; the timeline still names the older ones.
            CHECK( predictor.GetMispredicts().size() == NetPredictor::kMaxMispredicts );
            CHECK( predictor.GetMispredicts().front().Tick == 80 - NetPredictor::kMaxMispredicts );
            CHECK( predictor.FindMispredict( 10 ) == nullptr );
            CHECK( predictor.GetOutcome( 10 ) == NetPredictOutcome::Correction );
            CHECK( predictor.GetOutcome( 79 ) == NetPredictOutcome::Correction );
        }
        if( tick == 79 + span - 1 )
        {
            // 79 is still on the timeline (the newest 600 ticks: 79..678), so its record stays.
            CHECK( predictor.GetOutcome( 79 ) == NetPredictOutcome::Correction );
            REQUIRE( predictor.GetMispredicts().size() == 1 );
            CHECK( predictor.GetMispredicts().front().Tick == 79 );
        }
        if( tick == 79 + span )
        {
            // A match a full timeline later takes it off: no new mismatch needed.
            CHECK( predictor.GetOutcome( 79 ) == NetPredictOutcome::None );
            CHECK( predictor.GetMispredicts().empty() );
            CHECK( predictor.FindMispredict( 79 ) == nullptr );
        }
    }
    CHECK( predictor.GetMatchCount() == 10 + static_cast<int>( span ) + 10 );
}


TEST_CASE( "Net: a misprediction with NaN / infinity round-trips through JSON, and a malformed file is an error, not a throw" )
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    NetMispredictRecord record;
    record.Tick = 726;
    record.Predicted.Position = Vector3( 1.f, 2.f, 3.f );
    record.Predicted.Velocity = Vector3( nan, inf, -inf );     // a blown-up mover
    record.Authoritative.Position = Vector3( 1.f, 2.f, 3.f );
    record.Authoritative.Velocity = Vector3( 0.1f, -9.81f, 1e-7f );
    InputCommand command;
    command.Tick = 726;
    command.Axes[0] = nan;
    command.Yaw = inf;
    command.ViewTick = -std::numeric_limits<double>::infinity();
    record.Commands.push_back( command );
    record.ResimMilliseconds = std::numeric_limits<double>::quiet_NaN();

    const std::string text = NetMispredict::ToJson( record );
    CHECK( text.find( "null" ) == std::string::npos );
    NetMispredictRecord loaded;
    std::string error;
    REQUIRE_MESSAGE( NetMispredict::FromJson( text, loaded, &error ), error );
    CHECK( loaded.Tick == 726 );
    CHECK( std::isnan( loaded.Predicted.Velocity.x ) );
    CHECK( loaded.Predicted.Velocity.y == inf );
    CHECK( loaded.Predicted.Velocity.z == -inf );
    CHECK( loaded.Authoritative == record.Authoritative );     // finite floats exactly
    REQUIRE( loaded.Commands.size() == 1 );
    CHECK( std::isnan( loaded.Commands[0].Axes[0] ) );
    CHECK( loaded.Commands[0].Yaw == inf );
    CHECK( loaded.Commands[0].ViewTick == -std::numeric_limits<double>::infinity() );
    CHECK( std::isnan( loaded.ResimMilliseconds ) );

    // Hand-edited files: wrong field types come back false with a reason.
    for( const char* bad : { R"({"format":1,"tick":"726"})", R"({"format":1,"scriptDiffs":[["a",2]]})", R"({"format":1,"predicted":5})",
             R"({"format":1,"commands":[{"axes":3}]})", R"({"format":"1"})", R"([1,2])", "not json" } )
    {
        error.clear();
        CHECK_MESSAGE( !NetMispredict::FromJson( bad, loaded, &error ), bad );
        CHECK_MESSAGE( !error.empty(), bad );
    }
    // A null where a number goes (an old export's NaN) reads as the default, not a throw.
    REQUIRE( NetMispredict::FromJson( R"({"format":1,"predicted":{"velocity":[null,1,2]}})", loaded, &error ) );
    CHECK( ( loaded.Predicted.Velocity == Vector3( 0.f, 1.f, 2.f ) ) );
}
