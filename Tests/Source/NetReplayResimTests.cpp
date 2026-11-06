#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Components/Transform.h"
#include "Core/UpdateContext.h"
#include "Cores/NetCore.h"
#include "Cores/PhysicsCore.h"
#include "Engine/World.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayResim.h"
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

// ME-316 phase 3: the resim check. A character's recorded movement, simulated again from a recorded
// mover with its owner's recorded commands, matches the recording tick for tick, and the ticks
// where the server did something the commands don't explain are found exactly.
namespace NetReplayResimTest
{
    struct TestContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

    constexpr uint64_t kPawnGuid = 0x9A3E;
    constexpr uint32_t kPawnId = 1;     // the only placed networked entity: its static NetId
    constexpr uint32_t kTicks = 300;
    constexpr uint32_t kKnockback = 150;    // the server shoves the pawn (a hit)
    constexpr uint32_t kRefill = 200;       // the server changes its predicted state (a pickup)
    constexpr uint32_t kGapFrom = 250;      // the owner's commands are missing (it left and came back)
    constexpr uint32_t kGapTo = 254;

    // The test's pawn: axes 0/1 steer on XZ, button 1 jumps (NetPredictionTests' course).
    void DrivePawn( Entity& InEntity, const InputCommand& InCommand )
    {
        CharacterController& controller = InEntity.GetComponent<CharacterController>();
        controller.SetMoveInput( Vector3( InCommand.Axes[0], 0.f, InCommand.Axes[1] ) );
        if( InCommand.Buttons & 1u )
        {
            controller.Jump();
        }
    }

    InputCommand CourseCommand( uint32_t InTick )
    {
        InputCommand command;
        command.Tick = InTick;
        switch( ( InTick / 60 ) % 4 )
        {
        case 0: command.Axes[1] = 1.f; break;
        case 1: command.Axes[0] = 1.f; break;
        case 2: command.Axes[0] = -0.7f; command.Axes[1] = -0.7f; break;
        default: command.Axes[0] = std::sin( InTick * 0.05f ); command.Axes[1] = 0.3f; break;
        }
        command.Buttons = InTick % 75 == 0 ? 1u : 0u;
        return command;
    }

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

    EntityHandle PlacePawn( World& InWorld )
    {
        EntityHandle pawn = InWorld.CreateEntityWithGUID( kPawnGuid );
        pawn->SetName( "Pawn" );
        pawn->AddComponent<Transform>().SetPosition( Vector3( 0.f, 0.9f, 0.f ) );
        pawn->AddComponent<NetIdentity>();
        pawn->AddComponent<CharacterController>();
        return pawn;
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

    std::vector<uint8_t> Bytes( uint32_t InValue )
    {
        std::vector<uint8_t> bytes( 4 );
        std::memcpy( bytes.data(), &InValue, 4 );
        return bytes;
    }

    uint32_t Value( const std::vector<uint8_t>& InBytes )
    {
        uint32_t value = 0;
        if( InBytes.size() == 4 )
        {
            std::memcpy( &value, InBytes.data(), 4 );
        }
        return value;
    }

    // The "server": the pawn on the course, driven by client 1's commands, its stand-in [Predicted]
    // state a counter that ticks up each step (5 more at kRefill, from the server alone), a shove at
    // kKnockback, and no command from client 1 at kGapFrom..kGapTo. Chunks of 100 ticks.
    std::unique_ptr<NetReplayMemorySource> RecordCourse()
    {
        PhysicsCore physics;
        auto world = std::make_shared<World>();
        world->IsLoading = false;
        world->AddCore( physics );
        BuildCourse( *world );
        EntityHandle pawn = PlacePawn( *world );
        CharacterController& controller = pawn->GetComponent<CharacterController>();
        world->Simulate();
        world->Start();

        NetReplayHeader header;
        header.Product = "ResimTest 1.0";
        header.TickRate = 60;
        header.KeyframeInterval = 100;
        auto source = std::make_unique<NetReplayMemorySource>( header );
        std::vector<NetReplayTick> chunk;
        uint32_t counter = 0;
        for( uint32_t t = 0; t < kTicks; ++t )
        {
            const InputCommand command = CourseCommand( t );
            DrivePawn( *pawn.Get(), command );
            ++counter;
            if( t == kKnockback )
            {
                controller.SetVelocity( controller.GetVelocity() + Vector3( 6.f, 3.f, 0.f ) );
            }
            if( t == kRefill )
            {
                counter += 5;
            }
            Step( physics, *world );

            NetReplayTick tick;
            tick.Tick = t;
            tick.State.Tick = t;
            tick.State.Entities[kPawnId].SetPose( pawn->GetComponent<Transform>().GetWorldPosition(), Quaternion() );
            if( t % 100 == 0 )
            {
                tick.Entities.push_back( { kPawnId, "Pawn", "", 1 } );
            }
            if( t < kGapFrom || t > kGapTo )
            {
                NetReplayClient& client = tick.Clients.emplace_back();
                client.Id = 1;
                client.Command = command;
            }
            NetReplayMover& mover = tick.Movers.emplace_back();
            mover.NetId = kPawnId;
            mover.State = controller.GetNetState();
            mover.Predicted = Bytes( counter );
            chunk.push_back( std::move( tick ) );
            if( chunk.size() == 100 )
            {
                REQUIRE( source->AddChunk( NetReplayChunk::Encode( chunk, NetReplayCodec::Deflate ) ) );
                chunk.clear();
            }
        }
        return source;
    }

    void CheckCourse( const NetReplayResim::Result& InResult )
    {
        INFO( NetReplayResim::Describe( InResult, 16 ) );
        CHECK( InResult.Error.empty() );
        CHECK( InResult.Name == "Pawn" );
        CHECK( InResult.Client == 1 );
        CHECK( InResult.FirstSeed == 0 );
        // 1..249 and 256..299 (seeded again at 255, after the gap).
        CHECK( InResult.Ticks == ( kGapFrom - 1 ) + ( kTicks - 1 - ( kGapTo + 1 ) ) );
        CHECK( InResult.Gaps == 1 );
        CHECK( InResult.ComparedPredicted );
        REQUIRE( InResult.DivergenceCount == 2 );
        REQUIRE( InResult.Divergences.size() == 2 );
        CHECK( InResult.Divergences[0].Tick == kKnockback );
        CHECK( InResult.Divergences[0].What.find( "velocity off by" ) != std::string::npos );
        CHECK( InResult.Divergences[0].What.find( "[Predicted]" ) == std::string::npos );
        CHECK( InResult.Divergences[1].Tick == kRefill );
        CHECK( InResult.Divergences[1].What == "[Predicted] bytes 0..1 differ (4 resimulated, 4 recorded)" );
        CHECK( InResult.Seeds == 4 );   // the start, after each divergence, after the gap
    }
}

using namespace NetReplayResimTest;


TEST_CASE( "Replay resim check: a character simulated again from its recorded mover with its owner's commands matches the recording; the server's own shove and state change are the divergent ticks (ME-316)" )
{
    std::unique_ptr<NetReplayMemorySource> source = RecordCourse();
    NetReplayReader reader( *source );

    // The viewer's side: the same course, the pawn simulated by the hooks.
    PhysicsCore physics;
    auto world = std::make_shared<World>();
    world->IsLoading = false;
    world->AddCore( physics );
    BuildCourse( *world );
    EntityHandle pawn = PlacePawn( *world );
    CharacterController& controller = pawn->GetComponent<CharacterController>();
    world->Simulate();
    world->Start();
    Step( physics, *world );    // the course's bodies
    uint32_t counter = 0;
    std::map<uint32_t, InputCommand> commands;
    NetReplayResim::Hooks hooks;
    hooks.Restore = [&]( const CharacterController::NetState& InState, const std::vector<uint8_t>& InPredicted ) {
        controller.SetNetState( InState );
        counter = Value( InPredicted );
    };
    hooks.Step = [&]( uint32_t InTick, const InputCommand& InCommand, const InputCommand& InPrevious ) {
        commands[InTick] = InCommand;
        CHECK( InPrevious.Tick + 1 == InTick );
        DrivePawn( *pawn.Get(), InCommand );
        ++counter;
        physics.StepCharacter( *pawn.Get(), controller, 1.f / 60.f );
    };
    hooks.Save = [&]( CharacterController::NetState& OutState, std::vector<uint8_t>& OutPredicted ) {
        OutState = controller.GetNetState();
        OutPredicted = Bytes( counter );
    };

    const NetReplayResim::Result whole = NetReplayResim::Run( reader, kPawnId, 0, kTicks - 1, hooks );
    CheckCourse( whole );
    // Each tick ran with the command the server ran.
    CHECK( commands.at( 37 ).Axes == CourseCommand( 37 ).Axes );
    CHECK( commands.at( 225 ).Buttons == CourseCommand( 225 ).Buttons );
    CHECK( commands.count( 252 ) == 0 );

    // From a keyframe past both: every tick matches.
    const NetReplayResim::Result tail = NetReplayResim::Run( reader, kPawnId, 200, 249, hooks );
    CHECK( tail.Error.empty() );
    CHECK( tail.FirstSeed == 200 );
    CHECK( tail.Ticks == 49 );
    CHECK_FALSE( tail.Diverged() );
    CHECK( NetReplayResim::Describe( tail ).find( "every one matches the recording" ) != std::string::npos );

    // Without the [Predicted] comparison, only the shove.
    NetReplayResim::Settings moverOnly;
    moverOnly.ComparePredicted = false;
    const NetReplayResim::Result movers = NetReplayResim::Run( reader, kPawnId, 0, kTicks - 1, hooks, moverOnly );
    REQUIRE( movers.DivergenceCount == 1 );
    CHECK( movers.Divergences[0].Tick == kKnockback );

    // What can't run says why.
    CHECK( NetReplayResim::Run( reader, 77, 0, 99, hooks ).Error.find( "#77" ) != std::string::npos );
    CHECK( NetReplayResim::Run( reader, kPawnId, 252, 254, hooks ).Error.find( "owner's command" ) != std::string::npos );
    CHECK( NetReplayResim::FindCharacters( reader, 0, kTicks - 1 ) == std::vector<uint32_t>{ kPawnId } );
}


TEST_CASE( "Replay resim check: NetCore::ResimulateReplay runs the world's command handler, the predicted scripts (recorded command, resimulating) and StepCharacter, then puts the character back (ME-316)" )
{
    std::unique_ptr<NetReplayMemorySource> source = RecordCourse();

    PhysicsCore physics;    // the cores before the world
    NetCore net;
    auto world = std::make_shared<World>();
    world->IsLoading = false;
    world->AddCore( physics );
    world->AddCore( net );
    BuildCourse( *world );
    EntityHandle pawn = PlacePawn( *world );
    world->Simulate();
    world->Start();
    Step( physics, *world );    // the course's bodies

    // A stand-in script: [Predicted] state a counter; its fixed step checks it runs resimulating
    // with the recorded tick's command.
    uint32_t counter = 0;
    int steps = 0;
    int wrongCommands = 0;
    NetCore::ScriptGlue glue;
    glue.State.SavePredicted = [&]( Entity&, std::vector<uint8_t>& OutBytes ) { OutBytes = Bytes( counter ); };
    glue.RestorePredicted = [&]( Entity&, const std::vector<uint8_t>& InBytes ) { counter = Value( InBytes ); };
    glue.SimulatePredicted = [&]( Entity& InEntity ) {
        ++steps;
        ++counter;
        InputCommand command;
        InputCommand previous;
        if( !net.IsResimulating() || !net.IsPredicted( InEntity ) || !net.GetCommand( InEntity, command, previous ) || command.Axes != CourseCommand( net.GetTick() ).Axes
            || previous.Tick + 1 != net.GetTick() )
        {
            ++wrongCommands;
        }
    };
    net.SetScriptGlue( glue );
    net.SetCommandHandler( DrivePawn );

    // Nothing plays yet.
    CHECK( net.ResimulateReplay( kPawnId, 0, 99 ).Error == "no replay is playing" );

    std::string error;
    REQUIRE_MESSAGE( net.StartPlayback( std::move( source ), &error ), error );
    NetReplayPlayback& playback = *net.GetPlayback();
    playback.Seek( 120 );
    net.WriteInterpolated( 0.0 );
    CHECK( net.GetTick() == 120 );
    CharacterController& controller = pawn->GetComponent<CharacterController>();
    counter = 999;
    const CharacterController::NetState shown = controller.GetNetState();

    const NetReplayResim::Result result = net.ResimulateReplay( kPawnId, 0, kTicks - 1 );
    CheckCourse( result );
    CHECK( steps == static_cast<int>( result.Ticks ) );
    CHECK( wrongCommands == 0 );
    // Put back as the tick shown has it; nothing resimulates any more.
    CHECK( controller.GetNetState() == shown );
    CHECK( counter == 999 );
    CHECK_FALSE( net.IsResimulating() );
    CHECK_FALSE( net.IsPredicted( *pawn.Get() ) );
    CHECK( net.GetTick() == 120 );
    InputCommand command;
    InputCommand previous;
    CHECK_FALSE( net.GetCommand( *pawn.Get(), command, previous ) );

    CHECK( net.ResimulateReplay( 42, 0, 99 ).Error.find( "isn't in the world" ) != std::string::npos );
    net.Shutdown();
}
