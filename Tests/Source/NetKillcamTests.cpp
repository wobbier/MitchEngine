#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetHitbox.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Components/Camera.h"
#include "Components/Graphics/Model.h"
#include "Components/Networking/SingletonKillcam.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Engine/KillcamPlayer.h"
#include "Events/EventReceiver.h"
#include "Net/NetKillcam.h"
#include "Net/NetReplayPlayback.h"
#include "Net/NetReplication.h"
#include "NetHarness.h"
#include "Scripting/Bindings/BindingContext.h"
#include "Scripting/Bindings/Systems/Net.bindings.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

// Killcams (ME-323 phase 2): cut from the server's replay ring for the killer's view, streamed to
// the victim over the blob channel, reassembled into a replay source.
namespace NetKillcamTest
{
    constexpr uint32_t kKiller = 1;
    constexpr uint32_t kVictim = 2;
    constexpr uint32_t kFar = 3;            // AlwaysRelevant, far away
    constexpr uint32_t kArrow = 4;          // the killer's client owns it, far away
    constexpr uint32_t kFirstMover = 10;    // 30 of them along X, every 10 m
    constexpr uint32_t kSpawned = 0x10000;  // comes and goes near the killer
    constexpr float kRadius = 50.f;

    Vector3 KillerAt( uint32_t InTick )
    {
        // Walks along X, 0.25 m a tick: what's in its view changes.
        return Vector3( static_cast<float>( InTick ) * 0.25f - 75.f, 1.f, 0.f );
    }

    // A match-like tick: the killer (client 2's), the victim (client 1's), movers, a spawned
    // entity, shots by the killer and by a far mover, events about near and far entities.
    NetReplayTick MakeTick( uint32_t InTick )
    {
        NetReplayTick tick;
        tick.Tick = InTick;
        tick.State.Tick = InTick;
        auto place = [&]( uint32_t InId, const Vector3& InAt, uint8_t InField ) {
            NetEntityState& state = tick.State.Entities[InId];
            state.SetPose( InAt, Quaternion() );
            state.Fields = { InField, static_cast<uint8_t>( InTick ) };
        };
        place( kKiller, KillerAt( InTick ), 1 );
        place( kVictim, Vector3( 20.f, 1.f, 5.f + std::sin( InTick * 0.05f ) ), 2 );
        place( kFar, Vector3( 900.f, 0.f, 0.f ), 3 );
        place( kArrow, Vector3( -900.f, 3.f, static_cast<float>( InTick ) * 0.1f ), 4 );
        for( uint32_t i = 0; i < 30; ++i )
        {
            place( kFirstMover + i, Vector3( static_cast<float>( i ) * 10.f - 150.f, 0.f, std::cos( InTick * 0.02f + i ) * 3.f ), 10 );
        }
        // A pickup near the killer, there for 50 ticks in every 100.
        if( InTick % 100 < 50 )
        {
            place( kSpawned + InTick / 100, KillerAt( InTick ) + Vector3( 5.f, 0.f, 0.f ), 9 );
        }
        if( InTick % 100 == 0 )
        {
            tick.Entities.push_back( { kSpawned + InTick / 100, "Pickup", "Assets/Prefabs/Pickup.prefab", 0 } );
        }
        // Mover 12 changes owner now and then (a vehicle).
        if( InTick % 150 == 75 )
        {
            tick.Entities.push_back( { kFirstMover + 12, "Mover12", "", static_cast<uint8_t>( ( InTick / 150 ) % 2 ? 2 : 0 ) } );
        }

        for( uint8_t id : { uint8_t( 1 ), uint8_t( 2 ) } )
        {
            NetReplayClient& client = tick.Clients.emplace_back();
            client.Id = id;
            client.Command.Tick = InTick;
            client.Command.Buttons = id == 2 && InTick % 20 == 0 ? 1u : 0u;
            client.Command.Axes = { 1.f, 0.f, 0.f, 0.f };
            client.Command.HasAim = true;
            client.Command.Yaw = NetCommands::QuantizeYaw( static_cast<float>( ( InTick * ( id == 2 ? 3 : 1 ) ) % 360 ) );
            client.Command.Pitch = NetCommands::QuantizePitch( id == 2 ? -5.f : 10.f );
            client.Command.ViewTick = static_cast<double>( InTick ) - 6.0;
            client.RoundTripMs = 150.f;
            client.Slack = 2.f;
        }
        for( uint32_t id : { kKiller, kVictim } )
        {
            NetReplayMover& mover = tick.Movers.emplace_back();
            mover.NetId = id;
            mover.State.Position = tick.State.Entities[id].GetPosition();
            mover.State.Velocity = Vector3( 15.f, 0.f, 0.f );
            mover.Predicted = { static_cast<uint8_t>( id ), static_cast<uint8_t>( InTick / 10 ) };
        }
        if( InTick % 10 == 0 )
        {
            // The killer shoots at the victim; a far mover shoots at nothing near.
            for( uint32_t shooter : { kKiller, kFirstMover + 29 } )
            {
                NetReplayShot& shot = tick.Shots.emplace_back();
                shot.Client = shooter == kKiller ? 2 : 0;
                shot.Shooter = shooter;
                shot.Origin = tick.State.Entities[shooter].GetPosition();
                shot.Direction = Vector3( 1.f, 0.f, 0.f );
                shot.Range = 200.f;
                shot.Hit = shooter == kKiller && InTick % 20 == 0;
                shot.Target = shot.Hit ? kVictim : 0;
                for( uint32_t id : { kVictim, kFirstMover + 29, 0u } )
                {
                    NetReplayHitbox& hitbox = shot.Hitboxes.emplace_back();
                    hitbox.NetId = id;
                    hitbox.Name = id == 0 ? "Door" : "Body";
                    hitbox.Position = id == 0 ? Vector3() : tick.State.Entities[id].GetPosition();
                }
            }
        }
        if( InTick % 50 == 0 )
        {
            tick.Events.push_back( { "damage", "{\"amount\":10}", kVictim } );
            tick.Events.push_back( { "pick", "{}", kFirstMover + 29 } );
            tick.Events.push_back( { "round", "{}", 0 } );
        }
        return tick;
    }

    // What the killer's client would have been sent at a tick (NET-13's rule), and the victim.
    std::set<uint32_t> Relevant( const NetReplayTick& InTick, const std::map<uint32_t, uint8_t>& InOwners, uint8_t InKillerClient, float InRadius,
        const std::set<uint32_t>& InAlways, const Vector3& InViewer, uint32_t InKiller, uint32_t InVictim )
    {
        std::set<uint32_t> relevant;
        for( const auto& [id, state] : InTick.State.Entities )
        {
            auto owner = InOwners.find( id );
            const bool owned = InKillerClient != 0 && owner != InOwners.end() && owner->second == InKillerClient;
            if( id == InKiller || id == InVictim || InAlways.count( id ) || owned || ( state.GetPosition() - InViewer ).Length() <= InRadius )
            {
                relevant.insert( id );
            }
        }
        return relevant;
    }

    // What the killer's client holds after a tick: the states in view as they are, the rest as it
    // last had them (in view, or as the ring first has them); what's gone, gone.
    void Hold( std::map<uint32_t, NetEntityState>& InOut, const NetReplayTick& InTick, const std::set<uint32_t>& InRelevant )
    {
        for( const auto& [id, state] : InTick.State.Entities )
        {
            if( InRelevant.count( id ) || !InOut.count( id ) )
            {
                InOut[id] = state;
            }
        }
        for( auto it = InOut.begin(); it != InOut.end(); )
        {
            it = InTick.State.Entities.count( it->first ) ? std::next( it ) : InOut.erase( it );
        }
    }

    struct TestContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

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

    NetConditions Link( float InLatencyMs, float InJitterMs, float InLoss, uint64_t InSeed )
    {
        NetConditions conditions;
        conditions.LatencyMs = InLatencyMs;
        conditions.JitterMs = InJitterMs;
        conditions.Loss = InLoss;
        conditions.Seed = InSeed;
        return conditions;
    }
}

using namespace NetKillcamTest;
using namespace NetHarness;

TEST_CASE( "Net: a killcam cut from the replay ring keeps what the killer's client was sent, its commands, mover, shots and events, and reassembles in any order" )
{
    // A ring-only recorder (5 s) after 600 ticks: its oldest tick is 300, so the ring's own NetId
    // table has moved on from the keyframe it started with.
    NetReplayHeader header;
    header.TickRate = 60;
    header.Map = "Assets/Scenes/Arena.lvl";
    header.CommandLine = "Player --server 0 --secret";
    NetReplaySettings settings;
    settings.RingSeconds = 5.0;
    settings.KeyframeInterval = 100;
    NetReplayRecorder recorder( header, settings );
    std::map<uint32_t, uint8_t> owners;     // NetId -> owner, as of each tick (the test's own table)
    std::map<uint32_t, std::map<uint32_t, uint8_t>> ownersAt;
    for( uint32_t t = 0; t < 600; ++t )
    {
        NetReplayTick tick = MakeTick( t );
        if( t == 0 )
        {
            for( const auto& [id, state] : tick.State.Entities )
            {
                const uint8_t owner = id == kKiller || id == kArrow ? 2 : id == kVictim ? 1 : 0;
                tick.Entities.push_back( { id, "E" + std::to_string( id ), "", owner } );
            }
        }
        for( const NetReplayEntity& entity : tick.Entities )
        {
            owners[entity.NetId] = entity.Owner;
        }
        for( auto it = owners.begin(); it != owners.end(); )
        {
            it = tick.State.Entities.count( it->first ) ? std::next( it ) : owners.erase( it );
        }
        ownersAt[t] = owners;
        recorder.Record( std::move( tick ) );
    }
    REQUIRE( recorder.GetRing().size() == 300 );
    REQUIRE( recorder.GetRing().front()->Tick == 300 );
    // The ring's table as of its oldest tick is the test's.
    std::map<uint32_t, uint8_t> ringOwners;
    for( const auto& [id, entity] : recorder.GetRingEntities() )
    {
        ringOwners[id] = entity.Owner;
    }
    CHECK( ringOwners == ownersAt[300] );

    NetKillcamRequest request;
    request.Id = 7;
    request.KillerNetId = kKiller;
    request.VictimNetId = kVictim;
    request.DeathTick = 590;
    request.FirstTick = 590 - 239;     // 4 s
    request.RelevantRadius = kRadius;
    request.AlwaysRelevant = { kFar };
    request.ChunkTicks = 60;
    request.Header = recorder.GetHeader();
    const std::vector<std::shared_ptr<const NetReplayTick>> ring( recorder.GetRing().begin(), recorder.GetRing().end() );
    const NetKillcam::Encoded encoded = NetKillcam::Encode( request, ring, recorder.GetRingEntities() );
    REQUIRE( encoded.Error.empty() );
    CHECK( encoded.FirstTick == 351 );
    CHECK( encoded.LastTick == 590 );
    CHECK( encoded.Ticks == 240 );
    REQUIRE( encoded.Blobs.size() == 4 );
    size_t total = 0;
    for( const auto& blob : encoded.Blobs )
    {
        total += blob.size();
    }
    CHECK( total == encoded.Bytes );
    MESSAGE( "a 4 s killcam: " << encoded.Bytes << " bytes in " << encoded.Blobs.size() << " blobs (" << encoded.RawBytes << " before deflate), "
                               << encoded.Milliseconds << " ms" );

    // Chunks 2 and 1 arrive before chunk 0 (and 1 twice): nothing is readable until 0 is in, then 0-2 are.
    NetKillcamReceiver receiver;
    const std::vector<uint8_t> other = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 };
    CHECK( receiver.Accept( other.data(), other.size() ) == NetKillcamReceiver::Result::NotKillcam );
    CHECK( receiver.Accept( encoded.Blobs[2].data(), encoded.Blobs[2].size() ) == NetKillcamReceiver::Result::Accepted );
    CHECK( receiver.Accept( encoded.Blobs[1].data(), encoded.Blobs[1].size() ) == NetKillcamReceiver::Result::Accepted );
    CHECK( receiver.Accept( encoded.Blobs[1].data(), encoded.Blobs[1].size() ) == NetKillcamReceiver::Result::Stale );
    CHECK( receiver.GetId() == 7 );
    CHECK_FALSE( receiver.HasHeader() );
    CHECK( receiver.GetChunksReady() == 0 );
    CHECK( receiver.GetTotalBytes() == encoded.Bytes );
    CHECK( receiver.Accept( encoded.Blobs[0].data(), encoded.Blobs[0].size() ) == NetKillcamReceiver::Result::Accepted );
    CHECK( receiver.GetChunksReady() == 3 );
    CHECK_FALSE( receiver.IsComplete() );
    // A torn one is refused and changes nothing.
    std::vector<uint8_t> torn( encoded.Blobs[3].begin(), encoded.Blobs[3].begin() + 40 );
    CHECK( receiver.Accept( torn.data(), torn.size() ) == NetKillcamReceiver::Result::Bad );
    CHECK( receiver.Accept( encoded.Blobs[3].data(), encoded.Blobs[3].size() ) == NetKillcamReceiver::Result::Accepted );
    REQUIRE( receiver.IsComplete() );
    CHECK( receiver.GetBytesReceived() == encoded.Bytes );
    CHECK( receiver.GetVictimNetId() == kVictim );
    CHECK( receiver.GetKillerNetId() == kKiller );
    CHECK( receiver.GetKillerClient() == 2 );
    CHECK( receiver.GetDeathTick() == 590 );
    CHECK( receiver.GetFirstTick() == 351 );
    REQUIRE( receiver.GetSource() );
    CHECK( receiver.GetSource()->GetHeader().TickRate == 60 );
    CHECK( receiver.GetSource()->GetHeader().Map == header.Map );
    CHECK( receiver.GetSource()->GetHeader().CommandLine.empty() );     // not the server's to share
    CHECK( receiver.GetSource()->GetHeader().KeyframeInterval == 60 );

    // Every tick against the ring, filtered by the test's own reading of the rule.
    NetReplayReader reader( *receiver.GetSource() );
    CHECK( reader.GetFirstTick() == 351 );
    CHECK( reader.GetLastTick() == 590 );
    std::set<uint32_t> previous;
    std::map<uint32_t, NetReplayEntity> named;      // what the stream has named so far (a playback's table)
    std::map<uint32_t, NetEntityState> held;        // what the killer's client has, from the ring's oldest tick
    for( uint32_t t = 300; t < 351; ++t )
    {
        Hold( held, *ring[t - 300], Relevant( *ring[t - 300], ownersAt[t], 2, kRadius, { kFar }, KillerAt( t ), kKiller, kVictim ) );
    }
    size_t filteredOut = 0;
    size_t heldBack = 0;
    size_t shotsKept = 0;
    size_t eventsKept = 0;
    for( uint32_t t = 351; t <= 590; ++t )
    {
        INFO( "tick " << t );
        const NetReplayTick& original = *ring[t - 300];
        NetReplayTick decoded;
        REQUIRE( reader.ReadTick( t, decoded ) );
        const std::set<uint32_t> relevant = Relevant( original, ownersAt[t], 2, kRadius, { kFar }, KillerAt( t ), kKiller, kVictim );
        // Every entity the tick has (out of view isn't destroyed: a playback mustn't despawn it), those
        // in view as they are, the rest as the killer's client last had them.
        Hold( held, original, relevant );
        CHECK( decoded.State.Entities == held );
        CHECK( decoded.State.Entities.size() == original.State.Entities.size() );
        for( const auto& [id, state] : original.State.Entities )
        {
            if( !relevant.count( id ) )
            {
                ++filteredOut;
                heldBack += decoded.State.Entities.at( id ) == state ? 0 : 1;
            }
        }
        CHECK( relevant.count( kFar ) );
        CHECK( relevant.count( kArrow ) );
        CHECK( relevant.count( kVictim ) );
        // The killer's client's command alone, exactly; the killer's mover alone.
        REQUIRE( decoded.Clients.size() == 1 );
        CHECK( decoded.Clients[0].Id == 2 );
        CHECK( decoded.Clients[0].Command == original.Clients[1].Command );
        CHECK( decoded.Clients[0].Command.Yaw == original.Clients[1].Command.Yaw );
        REQUIRE( decoded.Movers.size() == 1 );
        CHECK( decoded.Movers[0].NetId == kKiller );
        CHECK( decoded.Movers[0].State == original.Movers[0].State );
        CHECK( decoded.Movers[0].Predicted == original.Movers[0].Predicted );
        // Shots by (or at) what's in view, with the hitboxes in view and the unnetworked ones.
        size_t expectedShots = 0;
        for( const NetReplayShot& shot : original.Shots )
        {
            expectedShots += relevant.count( shot.Shooter ) || relevant.count( shot.Target ) ? 1 : 0;
        }
        REQUIRE( decoded.Shots.size() == expectedShots );
        for( const NetReplayShot& shot : decoded.Shots )
        {
            CHECK( relevant.count( shot.Shooter ) );
            for( const NetReplayHitbox& hitbox : shot.Hitboxes )
            {
                CHECK( ( hitbox.NetId == 0 || relevant.count( hitbox.NetId ) ) );
            }
        }
        shotsKept += decoded.Shots.size();
        size_t expectedEvents = 0;
        for( const NetReplayEvent& event : original.Events )
        {
            expectedEvents += event.Entity == 0 || relevant.count( event.Entity ) ? 1 : 0;
        }
        CHECK( decoded.Events.size() == expectedEvents );
        eventsKept += decoded.Events.size();
        // Named: everything at a chunk's start, then what comes into view, and owner changes.
        if( ( t - 351 ) % 60 == 0 )
        {
            named.clear();
        }
        for( const NetReplayEntity& entity : decoded.Entities )
        {
            named[entity.NetId] = entity;
        }
        std::set<uint32_t> present;
        for( const auto& [id, state] : decoded.State.Entities )
        {
            present.insert( id );
            REQUIRE( named.count( id ) );
            CHECK( named[id].Owner == ownersAt[t].at( id ) );
            if( !previous.count( id ) || ( t - 351 ) % 60 == 0 )
            {
                bool here = false;
                for( const NetReplayEntity& entity : decoded.Entities )
                {
                    here = here || entity.NetId == id;
                }
                CHECK( here );
            }
        }
        previous = present;
    }
    MESSAGE( filteredOut << " entity states out of view over 240 ticks (" << heldBack << " held back from the ring's); " << shotsKept << " shots and " << eventsKept << " events kept" );
    CHECK( filteredOut > 240 * 10 );
    CHECK( heldBack > 240 * 10 );
    CHECK( shotsKept > 0 );
    CHECK( eventsKept > 0 );

    // A newer killcam replaces it; the older one's late blobs are stale.
    request.Id = 8;
    request.FirstTick = 500;
    const NetKillcam::Encoded newer = NetKillcam::Encode( request, ring, recorder.GetRingEntities() );
    REQUIRE( newer.Blobs.size() == 2 );
    CHECK( receiver.Accept( newer.Blobs[1].data(), newer.Blobs[1].size() ) == NetKillcamReceiver::Result::Accepted );
    CHECK( receiver.GetId() == 8 );
    CHECK_FALSE( receiver.HasHeader() );
    CHECK( receiver.Accept( encoded.Blobs[0].data(), encoded.Blobs[0].size() ) == NetKillcamReceiver::Result::Stale );
    CHECK( receiver.Accept( newer.Blobs[0].data(), newer.Blobs[0].size() ) == NetKillcamReceiver::Result::Accepted );
    CHECK( receiver.IsComplete() );
    CHECK( receiver.GetFirstTick() == 500 );

    // Out of the ring, or a killer that isn't there: nothing, and why.
    request.FirstTick = 10;
    request.DeathTick = 200;
    CHECK_FALSE( NetKillcam::Encode( request, ring, recorder.GetRingEntities() ).Error.empty() );
    request.FirstTick = 400;
    request.DeathTick = 500;
    request.KillerNetId = 999;
    const NetKillcam::Encoded missing = NetKillcam::Encode( request, ring, recorder.GetRingEntities() );
    CHECK( missing.Blobs.empty() );
    CHECK( missing.Error.find( "#999" ) != std::string::npos );
}


TEST_CASE( "Net: at 150 ms RTT and 5% loss a kill streams the killer's 8 s to the victim whole, tick-exact against the ring, within the link's budget, with no extra corrections" )
{
    // ME-323's done-when, the stream half: client 1 predicts its pawn (the victim) on a course;
    // client 2's pawn (the killer) walks and turns with its aim and shoots it; 24 movers along X,
    // relevancy at 30 m, one far one AlwaysRelevant; the server keeps the replay ring.
    PhysicsCore serverPhysics;
    PhysicsCore clientPhysics;
    Harness net( Link( 75.f, 5.f, 0.05f, 323 ), Link( 75.f, 5.f, 0.05f, 324 ) );
    net.ServerWorld->AddCore( serverPhysics );
    net.ClientWorld->AddCore( clientPhysics );
    constexpr uint64_t kVictimGuid = 0x7A3E01;
    constexpr uint64_t kKillerGuid = 0x7A3E02;
    auto placePawns = [&]( World& InWorld, bool InCharacters ) {
        EntityHandle floor = InWorld.CreateEntity();
        floor->AddComponent<Transform>().SetPosition( Vector3( 0.f, -0.5f, 0.f ) );
        floor->AddComponent<BoxCollider>().Size = Vector3( 200.f, 1.f, 200.f );
        for( const auto& [guid, at] : { std::pair<uint64_t, Vector3>( kVictimGuid, Vector3( 0.f, 0.9f, 0.f ) ), std::pair<uint64_t, Vector3>( kKillerGuid, Vector3( -12.f, 0.9f, -6.f ) ) } )
        {
            EntityHandle pawn = InWorld.CreateEntityWithGUID( guid );
            pawn->AddComponent<Transform>().SetPosition( at );
            pawn->AddComponent<NetIdentity>();
            if( InCharacters )
            {
                pawn->AddComponent<CharacterController>();
            }
        }
    };
    placePawns( *net.ServerWorld, true );
    placePawns( *net.ClientWorld, true );
    auto find = []( World& InWorld, uint64_t InGuid ) {
        EntityHandle found;
        InWorld.ForEachEntity( [&]( Entity& InEntity ) {
            if( InEntity.GetGUID() == InGuid )
            {
                found = InWorld.GetEntity( InEntity.GetId() );
            }
        } );
        return found;
    };
    EntityHandle victim = find( *net.ServerWorld, kVictimGuid );
    EntityHandle killer = find( *net.ServerWorld, kKillerGuid );
    EntityHandle clientPawn = find( *net.ClientWorld, kVictimGuid );
    victim->GetComponent<NetIdentity>().SetOwner( 1 );
    victim->AddComponent<NetHitbox>();
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        world->Simulate();
        world->Start();
    }
    std::vector<EntityHandle> movers;
    EntityHandle distant;

    auto drive = []( Entity& InEntity, const InputCommand& InCommand ) {
        CharacterController& controller = InEntity.GetComponent<CharacterController>();
        controller.SetMoveInput( Vector3( InCommand.Axes[0], 0.f, InCommand.Axes[1] ) );
    };
    net.ServerNet.SetCommandHandler( drive );
    net.ClientNet.SetCommandHandler( drive );
    Peer* killerPeer = nullptr;
    net.MakeCommand = [&]( uint32_t InTick ) {
        InputCommand command;
        if( killerPeer && net.Making == &killerPeer->Net )
        {
            // The killer circles slowly, turning its aim all the time.
            command.Axes[0] = 0.3f * std::sin( InTick * 0.02f );
            command.Axes[1] = 0.3f * std::cos( InTick * 0.02f );
            command.HasAim = true;
            command.Yaw = static_cast<float>( ( InTick * 3 ) % 360 );
            command.Pitch = -4.f;
            command.Buttons = InTick % 30 == 0 ? 1u : 0u;
            return command;
        }
        // The victim walks a slow square.
        switch( ( InTick / 120 ) % 4 )
        {
        case 0: command.Axes[1] = 0.5f; break;
        case 1: command.Axes[0] = 0.5f; break;
        case 2: command.Axes[1] = -0.5f; break;
        default: command.Axes[0] = -0.5f; break;
        }
        return command;
    };
#if USING( ME_SCRIPTING )
    ScriptEngineAPI api{};
    Register_NetBindings( api );
#endif
    uint32_t killTick = 0;
    bool killed = false;
    std::vector<std::shared_ptr<const NetReplayTick>> ringAtKill;
    std::map<uint32_t, NetReplayEntity> ringEntitiesAtKill;
    uint32_t shotsFired = 0;
    net.OnServerTick = [&]( NetServer& InServer ) {
        const float t = static_cast<float>( InServer.GetTick() ) / 60.f;
        for( size_t i = 0; i < movers.size(); ++i )
        {
            movers[i]->GetComponent<Transform>().SetPosition( Vector3( static_cast<float>( i ) * 8.f - 96.f, 0.f, std::sin( t + static_cast<float>( i ) ) * 4.f ) );
        }
        // The killer's shots at the victim, through lag compensation (the ring records them).
        const NetServer::Client* shooter = InServer.FindClient( 2 );
        if( shooter && shooter->Accepted && InServer.GetTick() % 30 == 0 )
        {
            const Vector3 from = killer->GetComponent<Transform>().GetWorldPosition() + Vector3( 0.f, 0.6f, 0.f );
            const Vector3 to = victim->GetComponent<Transform>().GetWorldPosition();
            net.ServerNet.LagCompensatedRaycast( from, ( to - from ).Normalized(), 100.f, shooter->Current, killer );
            ++shotsFired;
        }
        if( killTick != 0 && InServer.GetTick() == killTick )
        {
            net.ServerNet.RecordReplayEvent( "kill", "{\"killer\":\"Kestrel\",\"victim\":\"Striker\"}", victim );
#if USING( ME_SCRIPTING )
            // As C#'s Net.SendKillcam does it; a bot (owned by nobody) has nobody to send one to.
            ScriptBindings::WorldScope scope( net.ServerWorld );
            CHECK_FALSE( api.Net_SendKillcam( distant->GetId(), killer->GetId(), 8.f ) );
            REQUIRE( api.Net_SendKillcam( victim->GetId(), killer->GetId(), 8.f ) );
#else
            REQUIRE( net.ServerNet.SendKillcam( victim, killer, 8.0 ) );
#endif
            killed = true;
        }
        // The ring as the killcam is cut from it (the death tick is recorded by now).
        if( killed && ringAtKill.empty() && InServer.GetTick() == killTick + 1 )
        {
            const NetReplayRecorder& recorder = *net.ServerNet.GetRecorder();
            ringAtKill.assign( recorder.GetRing().begin(), recorder.GetRing().end() );
            ringEntitiesAtKill = recorder.GetRingEntities();
        }
        Step( serverPhysics, *net.ServerWorld );
    };
    net.OnClientTick = [&]( NetCore& ) { Step( clientPhysics, *net.ClientWorld ); };

    net.Start();
    // Dynamic: spawned to the clients (placed ones would have to be in every world).
    for( int i = 0; i < 24; ++i )
    {
        EntityHandle mover = net.ServerWorld->CreateEntity();
        mover->AddComponent<Transform>();
        mover->AddComponent<NetIdentity>();
        movers.push_back( mover );
    }
    distant = net.ServerWorld->CreateEntity();
    distant->AddComponent<Transform>().SetPosition( Vector3( 500.f, 0.f, 0.f ) );
    distant->AddComponent<NetIdentity>().AlwaysRelevant = true;
    net.ServerNet.GetReplicationServer()->Settings.RelevantRadius = 30.f;
    REQUIRE( net.ServerNet.StartRecording( "" ) );     // the ring alone
    net.ClientNet.Predict( clientPawn );
    // The victim's client is client 1, the killer's 2.
    net.Run( 1.5 );
    REQUIRE( net.ClientNet.GetClient()->GetClientId() == 1 );
    Peer& peer = net.AddClient( Link( 40.f, 2.f, 0.f, 325 ), [&]( World& InWorld ) {
        placePawns( InWorld, false );
        InWorld.Simulate();
        InWorld.Start();
    } );
    killerPeer = &peer;
    net.Run( 1.5 );
    INFO( net.ClientNet.GetClient()->GetRejectReason() );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    REQUIRE( peer.Net.GetClient()->GetState() == NetClient::State::Connected );
    REQUIRE( peer.Net.GetClient()->GetClientId() == 2 );
    killer->GetComponent<NetIdentity>().SetOwner( 2 );
    // 12 s of the match, then a window without a killcam to compare against.
    net.Run( 9.0 );
    NetPredictor& predictor = *net.ClientNet.GetPredictor();
    NetServer::Client& onServer = *net.ServerNet.GetServer()->GetClients().front();
    REQUIRE( onServer.Id == 1 );
    struct Counts
    {
        uint64_t Snapshots = 0;
        uint64_t Misses = 0;
        int Corrections = 0;
        int Resyncs = 0;
        uint64_t BytesDown = 0;
        double At = 0.0;
    };
    auto count = [&]() {
        Counts counts;
        counts.Snapshots = net.ClientNet.GetReplicationClient()->GetSnapshotsReceived();
        counts.Misses = onServer.Commands.GetMissedCount();
        counts.Corrections = predictor.GetCorrectionCount();
        counts.Resyncs = net.ClientNet.GetClient()->GetClock().GetResyncCount();
        counts.BytesDown = onServer.Connection->GetStats().BytesSent;
        counts.At = net.Now();
        return counts;
    };
    const Counts before = count();
    net.Run( 4.0 );
    const Counts baseline = count();
    const double baselineRate = static_cast<double>( baseline.Snapshots - before.Snapshots ) / ( baseline.At - before.At );

    // The kill, then until the victim has the killcam whole.
    killTick = net.ServerNet.GetServer()->GetTick() + 2;
    CHECK( net.ClientNet.GetKillcamProgress() == 0.f );
    double completeAt = -1.0;
    double firstChunkAt = -1.0;
    // The encode runs on the background lane in wall-clock time, which the harness's simulated
    // milliseconds outrun: the transfer is timed from when the server had the chunks.
    double encodedAt = -1.0;
    uint64_t bytesAtEncode = 0;
    float progressSeen = 0.f;
    bool progressRose = true;
    for( int i = 0; i < 1500 && completeAt < 0.0; ++i )
    {
        net.Run( 0.01 );
        const float progress = net.ClientNet.GetKillcamProgress();
        progressRose = progressRose && progress >= progressSeen;
        progressSeen = progress;
        if( encodedAt < 0.0 && net.ServerNet.GetKillcamStats().Encoded > 0 )
        {
            encodedAt = net.Now();
            bytesAtEncode = onServer.Connection->GetStats().BytesSent;
        }
        if( firstChunkAt < 0.0 && net.ClientNet.GetKillcam().GetChunksReady() > 0 )
        {
            firstChunkAt = net.Now();
        }
        if( net.ClientNet.GetKillcam().IsComplete() )
        {
            completeAt = net.Now();
        }
    }
    REQUIRE( killed );
    REQUIRE( completeAt > 0.0 );
    const Counts during = count();
    const double seconds = during.At - baseline.At;
    const double duringRate = static_cast<double>( during.Snapshots - baseline.Snapshots ) / seconds;
    REQUIRE( encodedAt > 0.0 );
    const double transfer = completeAt - encodedAt;
    const double linkRate = static_cast<double>( during.BytesDown - bytesAtEncode ) / transfer;
    const NetCore::KillcamStats& stats = net.ServerNet.GetKillcamStats();
    const NetKillcamReceiver& killcam = net.ClientNet.GetKillcam();
    MESSAGE( "killcam " << killcam.GetBytesReceived() << " bytes in " << killcam.GetChunkCount() << " chunks: encoded " << encodedAt - baseline.At << " s (simulated) after the kill (" << stats.EncodeMs
                        << " ms of wall clock), the first readable " << firstChunkAt - encodedAt << " s after, whole after " << transfer
                        << " s; snapshots " << baselineRate << "/s before, " << duringRate << "/s during; link " << linkRate
                        << " B/s; corrections " << baseline.Corrections - before.Corrections << " before, " << during.Corrections - baseline.Corrections
                        << " during; misses " << baseline.Misses - before.Misses << " then " << during.Misses - baseline.Misses << "; shots " << shotsFired );
    CHECK( progressRose );
    CHECK( net.ClientNet.GetKillcamProgress() == 1.f );
    CHECK( stats.Requested == 1 );
    CHECK( stats.Encoded == 1 );
    CHECK( stats.Failed == 0 );
    CHECK( killcam.GetId() == 1 );
    CHECK( killcam.GetChunkCount() == 8 );      // 480 ticks, 60 a chunk
    CHECK( killcam.GetDeathTick() == killTick );
    CHECK( killcam.GetFirstTick() == killTick - 479 );
    CHECK( killcam.GetKillerClient() == 2 );
    CHECK( firstChunkAt - encodedAt < 0.5 );
    CHECK( transfer < 2.0 );
    // Live traffic kept its budget: the snapshot rate, the commands, the clock, the prediction.
    CHECK( baselineRate > 50.0 );
    CHECK( std::abs( duringRate - baselineRate ) <= baselineRate * 0.05 );
    CHECK( static_cast<double>( during.Misses - baseline.Misses ) / seconds <= static_cast<double>( baseline.Misses - before.Misses ) / 4.0 + 0.5 );
    CHECK( during.Resyncs == baseline.Resyncs );
    CHECK( during.Corrections == baseline.Corrections );
    CHECK( linkRate <= onServer.Connection->GetBlobSettings().LinkBytesPerSecond * 1.03 );
#if USING( ME_SCRIPTING )
    // C#'s Net.Killcam on the victim's side: received, whole, the killer and victim as this world has them.
    {
        int id = 0;
        float progress = 0.f;
        EntityID killerHere;
        EntityID victimHere;
        {
            ScriptBindings::WorldScope scope( net.ClientWorld );
            CHECK( api.Net_GetKillcam( &id, &progress, &killerHere, &victimHere ) );
            CHECK_FALSE( api.Net_SendKillcam( clientPawn->GetId(), clientPawn->GetId(), 8.f ) );   // not the server
        }
        CHECK( id == 1 );
        CHECK( progress == 1.f );
        CHECK( victimHere == clientPawn->GetId() );
        const EntityHandle killerMirror = net.ClientNet.FindEntity( killer->GetComponent<NetIdentity>().GetNetId() );
        REQUIRE( killerMirror );
        CHECK( killerHere == killerMirror->GetId() );
        {
            ScriptBindings::WorldScope scope( net.ServerWorld );
            CHECK_FALSE( api.Net_GetKillcam( &id, &progress, &killerHere, &victimHere ) );
            CHECK( id == 0 );
        }
    }
#endif
    // The last blobs' acks come back; the server lets the killcam go.
    net.Run( 1.0 );
    CHECK( stats.Completed == 1 );
    CHECK( net.ServerNet.GetKillcamsPending() == 0 );

    // Tick for tick: the ring's states as the killer's client would have been sent them.
    REQUIRE( !ringAtKill.empty() );
    REQUIRE( ringAtKill.back()->Tick == killTick );
    const uint32_t killerId = killer->GetComponent<NetIdentity>().GetNetId();
    const uint32_t victimId = victim->GetComponent<NetIdentity>().GetNetId();
    const uint32_t farId = distant->GetComponent<NetIdentity>().GetNetId();
    NetReplayReader reader( *killcam.GetSource() );
    std::map<uint32_t, uint8_t> owners;
    for( const auto& [id, entity] : ringEntitiesAtKill )
    {
        owners[id] = entity.Owner;
    }
    Vector3 viewer;
    std::map<uint32_t, NetEntityState> held;
    uint32_t compared = 0;
    size_t filteredOut = 0;
    size_t heldBack = 0;
    size_t aimTurns = 0;
    size_t shotsSeen = 0;
    float lastYaw = -1.f;
    bool killEvent = false;
    for( size_t i = 0; i < ringAtKill.size(); ++i )
    {
        const NetReplayTick& original = *ringAtKill[i];
        if( i > 0 )
        {
            for( const NetReplayEntity& entity : original.Entities )
            {
                owners[entity.NetId] = entity.Owner;
            }
        }
        INFO( "tick " << original.Tick );
        viewer = original.State.Entities.at( killerId ).GetPosition();
        const std::set<uint32_t> relevant = Relevant( original, owners, 2, 30.f, { farId }, viewer, killerId, victimId );
        Hold( held, original, relevant );
        if( original.Tick < killcam.GetFirstTick() )
        {
            continue;
        }
        NetReplayTick decoded;
        REQUIRE( reader.ReadTick( original.Tick, decoded ) );
        CHECK( decoded.State.Entities == held );
        CHECK( decoded.State.Entities.size() == original.State.Entities.size() );
        for( const auto& [id, state] : original.State.Entities )
        {
            if( !relevant.count( id ) )
            {
                ++filteredOut;
                heldBack += decoded.State.Entities.at( id ) == state ? 0 : 1;
            }
        }
        REQUIRE( decoded.Clients.size() == 1 );
        CHECK( decoded.Clients[0].Id == 2 );
        for( const NetReplayClient& client : original.Clients )
        {
            if( client.Id == 2 )
            {
                CHECK( decoded.Clients[0].Command == client.Command );
            }
        }
        CHECK( decoded.Clients[0].Command.HasAim );
        aimTurns += decoded.Clients[0].Command.Yaw != lastYaw ? 1 : 0;
        lastYaw = decoded.Clients[0].Command.Yaw;
        REQUIRE( decoded.Movers.size() == 1 );
        CHECK( decoded.Movers[0].NetId == killerId );
        shotsSeen += decoded.Shots.size();
        for( const NetReplayShot& shot : decoded.Shots )
        {
            CHECK( shot.Shooter == killerId );
        }
        for( const NetReplayEvent& event : decoded.Events )
        {
            killEvent = killEvent || ( event.Name == "kill" && event.Entity == victimId && original.Tick == killTick );
        }
        ++compared;
    }
    MESSAGE( compared << " ticks compared; " << filteredOut << " states out of the killer's view (" << heldBack << " held back from the ring's); " << shotsSeen << " shots; aim changed on " << aimTurns << " ticks" );
    CHECK( compared == 480 );
    CHECK( filteredOut > 480 * 5 );
    CHECK( heldBack > 480 * 5 );
    CHECK( shotsSeen >= 15 );
    CHECK( aimTurns > 400 );
    CHECK( killEvent );
}


TEST_CASE( "Net: a bot's killcams: a newer one cancels the older, waiting or streaming; a victim who leaves takes its own; a projectile gone by the cut stays AlwaysRelevant; and played back, nothing out of view is despawned" )
{
    // Client 1's pawn is killed over and over by a bot (owned by nobody: no commands to send) at the
    // origin, with a 20 m view. Out of it: a placed post at 400 m (it moves; the killer's client holds
    // its pose) and an AlwaysRelevant arrow at 300 m that's destroyed before the kills.
    Harness net( Link( 50.f, 0.f, 0.f, 3231 ), Link( 50.f, 0.f, 0.f, 3232 ) );
    constexpr uint64_t kVictimGuid = 0x7A3E11;
    constexpr uint64_t kPostGuid = 0x7A3E12;
    net.Place( kVictimGuid );
    net.Place( kPostGuid );
    auto find = []( World& InWorld, uint64_t InGuid ) {
        EntityHandle found;
        InWorld.ForEachEntity( [&]( Entity& InEntity ) {
            if( InEntity.GetGUID() == InGuid )
            {
                found = InWorld.GetEntity( InEntity.GetId() );
            }
        } );
        return found;
    };
    EntityHandle victim = find( *net.ServerWorld, kVictimGuid );
    EntityHandle post = find( *net.ServerWorld, kPostGuid );
    victim->GetComponent<Transform>().SetPosition( Vector3( 5.f, 0.f, 0.f ) );
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        world->Simulate();
        world->Start();
    }
    EntityHandle arrow;
    net.OnServerTick = [&]( NetServer& InServer ) {
        const float t = static_cast<float>( InServer.GetTick() ) / 60.f;
        post->GetComponent<Transform>().SetPosition( Vector3( 400.f, 0.f, std::sin( t * 3.f ) * 5.f ) );
        if( arrow )
        {
            arrow->GetComponent<Transform>().SetPosition( Vector3( 300.f + t * 10.f, 2.f, 0.f ) );
        }
        net.ServerWorld->Simulate();
    };
    net.Start();
    net.Run( 1.0 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    REQUIRE( net.ClientNet.GetClient()->GetClientId() == 1 );
    victim->GetComponent<NetIdentity>().SetOwner( 1 );
    net.ServerNet.GetReplicationServer()->Settings.RelevantRadius = 20.f;
    REQUIRE( net.ServerNet.StartRecording( "" ) );
    // Three seconds of ring before the kills (1.5 s, the arrow's second, half a second): the last killcam
    // is cut 1.2-1.9 s after the kills start (the encodes before it take wall-clock time on the lane), and
    // a 3 s killcam's first tick must be in the ring, or its range is cut short to the ring's start (it
    // was, at 2.98 s, a run in five). The killer is there from the ring's start, so it's in every tick a
    // killcam can cover.
    EntityHandle killer = net.ServerWorld->CreateEntity();
    killer->SetName( "Bot" );
    killer->AddComponent<Transform>();
    killer->AddComponent<NetIdentity>();
    net.Run( 1.5 );
    arrow = net.ServerWorld->CreateEntity();
    arrow->AddComponent<Transform>().SetPosition( Vector3( 300.f, 2.f, 0.f ) );
    arrow->AddComponent<NetIdentity>().AlwaysRelevant = true;
    net.Run( 1.0 );
    const uint32_t arrowId = arrow->GetComponent<NetIdentity>().GetNetId();
    REQUIRE( arrowId != 0 );
    net.ServerWorld->DestroyEntity( arrow );
    arrow = EntityHandle();
    // Half a second on: the last killcam is cut 1.7-2.4 s after this (the encodes before it take
    // wall-clock time on the lane), so its 3 s usually hold all of the arrow's second. How much of it
    // they hold is the run's timing: the checks below ask for every tick it lived in the window, not a
    // count.
    net.Run( 0.5 );
    REQUIRE_FALSE( net.ServerNet.FindEntity( arrowId ) );

    const NetCore::KillcamStats& stats = net.ServerNet.GetKillcamStats();
    const NetKillcamReceiver& killcam = net.ClientNet.GetKillcam();
    NetServer::Client& onServer = *net.ServerNet.GetServer()->GetClients().front();
    auto runUntil = [&]( const std::function<bool()>& InDone, double InStep = 0.001, int InSteps = 3000 ) {
        for( int i = 0; i < InSteps && !InDone(); ++i )
        {
            net.Run( InStep );
        }
        return InDone();
    };

    // Two deaths a tick apart: the first killcam, waiting for its tick or encoding, gives way.
    REQUIRE( net.ServerNet.SendKillcam( victim, killer, 3.0 ) );
    net.Run( 0.02 );
    REQUIRE( net.ServerNet.SendKillcam( victim, killer, 3.0 ) );
    CHECK( stats.Cancelled == 1 );
    CHECK( net.ServerNet.GetKillcamsPending() == 1 );
    REQUIRE( runUntil( [&]() { return killcam.GetId() == 2 && killcam.IsComplete(); } ) );
    // Its last acks back: the server lets it go (a newer one before that would count it cancelled).
    REQUIRE( runUntil( [&]() { return stats.Completed == 1; } ) );
    CHECK( net.ServerNet.GetKillcamsPending() == 0 );

    // One streaming (its first chunk in, the rest waiting on its ack) gives way too: its blob is cancelled.
    const uint64_t blobsCancelled = onServer.Connection->GetStats().BlobsCancelled;
    REQUIRE( net.ServerNet.SendKillcam( victim, killer, 3.0 ) );
    REQUIRE( runUntil( [&]() { return killcam.GetId() == 3 && killcam.GetChunksReceived() > 0; } ) );
    CHECK_FALSE( killcam.IsComplete() );
    REQUIRE( net.ServerNet.SendKillcam( victim, killer, 3.0 ) );
    CHECK( stats.Cancelled == 2 );
    CHECK( onServer.Connection->GetStats().BlobsCancelled > blobsCancelled );
    REQUIRE( runUntil( [&]() { return killcam.GetId() == 4 && killcam.IsComplete(); } ) );
    CHECK( killcam.GetKillerClient() == 0 );
    // The ring as the killcam was cut from it.
    const std::vector<std::shared_ptr<const NetReplayTick>> ring( net.ServerNet.GetRecorder()->GetRing().begin(), net.ServerNet.GetRecorder()->GetRing().end() );
    net.Run( 0.5 );
    CHECK( stats.Completed == 2 );
    CHECK( net.ServerNet.GetKillcamsPending() == 0 );
    for( const NetBlobProgress& progress : onServer.Connection->GetBlobProgress() )
    {
        CHECK_FALSE( progress.Outgoing );
    }

    // The bot's killcam: nobody's commands; the arrow exact while it lived (AlwaysRelevant, though
    // destroyed before the cut); the post in every tick, held as the killer's client had it.
    const uint32_t killerId = killer->GetComponent<NetIdentity>().GetNetId();
    const uint32_t victimId = victim->GetComponent<NetIdentity>().GetNetId();
    const uint32_t postId = post->GetComponent<NetIdentity>().GetNetId();
    NetReplayReader reader( *killcam.GetSource() );
    REQUIRE( reader.GetFirstTick() == killcam.GetFirstTick() );
    REQUIRE( ring.front()->Tick <= killcam.GetFirstTick() );
    REQUIRE( ring.back()->Tick >= killcam.GetDeathTick() );
    const NetEntityState postAtFirst = ring.front()->State.Entities.at( postId );
    uint32_t compared = 0;
    uint32_t arrowTicks = 0;
    uint32_t postMoved = 0;
    // The arrow's life in the ring, and in the killcam's window.
    uint32_t arrowFirstInRing = 0;
    uint32_t arrowLastInRing = 0;
    uint32_t arrowFirst = 0;
    uint32_t arrowLast = 0;
    for( const auto& pointer : ring )
    {
        if( pointer->State.Entities.count( arrowId ) )
        {
            arrowFirstInRing = arrowFirstInRing ? arrowFirstInRing : pointer->Tick;
            arrowLastInRing = pointer->Tick;
        }
    }
    for( const auto& pointer : ring )
    {
        const NetReplayTick& original = *pointer;
        if( original.Tick < killcam.GetFirstTick() || original.Tick > killcam.GetDeathTick() )
        {
            continue;
        }
        INFO( "tick " << original.Tick );
        NetReplayTick decoded;
        REQUIRE( reader.ReadTick( original.Tick, decoded ) );
        std::set<uint32_t> ids;
        std::set<uint32_t> decodedIds;
        for( const auto& [id, state] : original.State.Entities )
        {
            ids.insert( id );
        }
        for( const auto& [id, state] : decoded.State.Entities )
        {
            decodedIds.insert( id );
        }
        CHECK( decodedIds == ids );
        CHECK( decoded.Clients.empty() );
        CHECK( decoded.State.Entities.at( killerId ) == original.State.Entities.at( killerId ) );
        CHECK( decoded.State.Entities.at( victimId ) == original.State.Entities.at( victimId ) );
        CHECK( decoded.State.Entities.at( postId ) == postAtFirst );
        postMoved += original.State.Entities.at( postId ) == postAtFirst ? 0 : 1;
        if( original.State.Entities.count( arrowId ) )
        {
            CHECK( decoded.State.Entities.at( arrowId ) == original.State.Entities.at( arrowId ) );
            arrowFirst = arrowFirst ? arrowFirst : original.Tick;
            arrowLast = original.Tick;
            ++arrowTicks;
        }
        ++compared;
    }
    MESSAGE( compared << " ticks of the bot's killcam (" << killcam.GetFirstTick() << "-" << killcam.GetDeathTick() << ", the ring from " << ring.front()->Tick
                       << "); the arrow in " << arrowTicks << " (it lived " << arrowFirstInRing << "-" << arrowLastInRing << "), the post held through "
                       << postMoved << " moves" );
    // The whole 3 s, every tick of it in the ring and the stream.
    CHECK( killcam.GetDeathTick() - killcam.GetFirstTick() + 1 == 180 );
    CHECK( compared == 180 );
    // The arrow in every tick it lived inside the window, out of view at 300 m (AlwaysRelevant), up to its
    // destruction before the cut: however much of its life the run's timing puts in the window.
    REQUIRE( arrowLastInRing != 0 );
    CHECK( arrowLastInRing < killcam.GetDeathTick() );
    CHECK( arrowTicks > 0 );
    CHECK( arrowFirst == std::max<uint32_t>( arrowFirstInRing, killcam.GetFirstTick() ) );
    CHECK( arrowLast == arrowLastInRing );
    CHECK( arrowTicks == arrowLast - arrowFirst + 1 );
    CHECK( postMoved > 100 );

    // Played into a world of its own (phase 3's way): the post and the killer are the same entities
    // from the first tick to the last, never despawned for being out of view.
    {
        NetCore replayNet;    // before the world
        auto replayWorld = std::make_shared<World>();
        replayWorld->IsLoading = false;
        replayWorld->AddCore( replayNet );
        for( uint64_t guid : { kVictimGuid, kPostGuid } )
        {
            EntityHandle entity = replayWorld->CreateEntityWithGUID( guid );
            entity->AddComponent<Transform>();
            entity->AddComponent<NetIdentity>();
        }
        auto source = std::make_unique<NetReplayMemorySource>( killcam.GetSource()->GetHeader() );
        for( size_t i = 0; i < killcam.GetSource()->GetChunkCount(); ++i )
        {
            std::vector<uint8_t> bytes;
            REQUIRE( killcam.GetSource()->ReadChunk( i, bytes ) );
            REQUIRE( source->AddChunk( std::move( bytes ) ) );
        }
        std::string error;
        REQUIRE_MESSAGE( replayNet.StartPlayback( std::move( source ), &error ), error );
        NetReplayPlayback& playback = *replayNet.GetPlayback();
        playback.SetPauseAt( playback.GetLastTick() );
        playback.Play();
        double now = 0.0;
        EntityID postHere;
        EntityID killerHere;
        int frames = 0;
        bool same = true;
        for( int frame = 0; frame < 400 && !playback.IsPaused(); ++frame )
        {
            now += 1.0 / 60.0;
            replayNet.Receive( now );
            replayNet.BeginTick();
            replayNet.EndTick( now );
            replayNet.WriteInterpolated( 1.0 / 60.0 );
            replayNet.Flush( now );
            replayWorld->Simulate();
            const EntityHandle postNow = replayNet.FindEntity( postId );
            const EntityHandle killerNow = replayNet.FindEntity( killerId );
            REQUIRE( postNow );
            REQUIRE( killerNow );
            if( frames++ == 0 )
            {
                postHere = postNow->GetId();
                killerHere = killerNow->GetId();
            }
            same = same && postNow->GetId() == postHere && killerNow->GetId() == killerHere;
        }
        CHECK( playback.IsPaused() );
        CHECK( replayNet.GetTick() == killcam.GetDeathTick() );
        CHECK( same );
        CHECK( frames > 170 );
        replayNet.Shutdown();
    }

    // A victim who leaves mid-stream takes its killcam with it.
    REQUIRE( net.ServerNet.SendKillcam( victim, killer, 3.0 ) );
    net.Run( 0.05 );
    net.ClientNet.GetClient()->Disconnect( net.Now() );
    net.Run( 0.5 );
    CHECK( stats.Cancelled == 3 );
    CHECK( stats.Requested == 5 );
    CHECK( stats.Failed == 0 );
    CHECK( net.ServerNet.GetKillcamsPending() == 0 );
}


TEST_CASE( "Net: the victim plays its killcam into a replay world of its own while the live world keeps its session: every tick the replay world shows is the ring's, the live client loses no snapshots and gains no corrections (ME-323 phase 3)" )
{
    // Phase 3's done-when: the killcam played, not just received. Client 1 predicts the victim, client 2's
    // pawn (the killer) circles turning its aim; 24 movers, a 30 m view, 150 ms RTT and 5% loss to the
    // victim. The victim's KillcamPlayer plays the stream into a second World (its own NetCore, the same
    // placed pawns, a camera) at 60 Hz beside the live session, from its first chunk.
    PhysicsCore serverPhysics;
    PhysicsCore clientPhysics;
    Harness net( Link( 75.f, 5.f, 0.05f, 3231 ), Link( 75.f, 5.f, 0.05f, 3232 ) );
    net.ServerWorld->AddCore( serverPhysics );
    net.ClientWorld->AddCore( clientPhysics );
    constexpr uint64_t kVictimGuid = 0x7A3E11;
    constexpr uint64_t kKillerGuid = 0x7A3E12;
    auto placePawns = [&]( World& InWorld, bool InCharacters ) {
        for( const auto& [guid, at] : { std::pair<uint64_t, Vector3>( kVictimGuid, Vector3( 0.f, 0.9f, 0.f ) ), std::pair<uint64_t, Vector3>( kKillerGuid, Vector3( -12.f, 0.9f, -6.f ) ) } )
        {
            EntityHandle pawn = InWorld.CreateEntityWithGUID( guid );
            pawn->AddComponent<Transform>().SetPosition( at );
            pawn->AddComponent<NetIdentity>();
            if( InCharacters )
            {
                pawn->AddComponent<CharacterController>();
            }
        }
    };
    auto floor = []( World& InWorld ) {
        EntityHandle ground = InWorld.CreateEntity();
        ground->AddComponent<Transform>().SetPosition( Vector3( 0.f, -0.5f, 0.f ) );
        ground->AddComponent<BoxCollider>().Size = Vector3( 200.f, 1.f, 200.f );
    };
    floor( *net.ServerWorld );
    floor( *net.ClientWorld );
    placePawns( *net.ServerWorld, true );
    placePawns( *net.ClientWorld, true );
    auto find = []( World& InWorld, uint64_t InGuid ) {
        EntityHandle found;
        InWorld.ForEachEntity( [&]( Entity& InEntity ) {
            if( InEntity.GetGUID() == InGuid )
            {
                found = InWorld.GetEntity( InEntity.GetId() );
            }
        } );
        return found;
    };
    EntityHandle victim = find( *net.ServerWorld, kVictimGuid );
    EntityHandle killer = find( *net.ServerWorld, kKillerGuid );
    EntityHandle clientPawn = find( *net.ClientWorld, kVictimGuid );
    victim->GetComponent<NetIdentity>().SetOwner( 1 );
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        world->Simulate();
        world->Start();
    }
    std::vector<EntityHandle> movers;
    auto drive = []( Entity& InEntity, const InputCommand& InCommand ) {
        if( CharacterController* controller = InEntity.TryGetComponent<CharacterController>() )
        {
            controller->SetMoveInput( Vector3( InCommand.Axes[0], 0.f, InCommand.Axes[1] ) );
        }
    };
    // The game's eye (ME-343, C#'s Net.Killcam.EyeHeight on the live world): the killcam's eye and the shot's origin.
    constexpr float kEyeHeight = 1.5f;
    net.ClientWorld->Singleton<SingletonKillcam>().EyeHeight = kEyeHeight;
    uint32_t killTick = 0;
    net.ServerNet.SetCommandHandler( [&]( Entity& InEntity, const InputCommand& InCommand ) {
        drive( InEntity, InCommand );
        // The kill: the killer's own lag-compensated shot from its eye along its aim, recorded at the death tick.
        if( killTick != 0 && net.ServerNet.GetTick() == killTick && InEntity.GetGUID() == kKillerGuid && InCommand.HasAim )
        {
            const Vector3 eye = InEntity.GetComponent<Transform>().GetWorldPosition() + Vector3( 0.f, kEyeHeight, 0.f );
            net.ServerNet.LagCompensatedRaycast( eye, KillcamPlayer::AimDirection( InCommand.Yaw, InCommand.Pitch ), 50.f, InCommand, killer );
        }
    } );
    net.ClientNet.SetCommandHandler( drive );
    Peer* killerPeer = nullptr;
    // The last second before the kill the killer stands and aims at the victim as its screen shows it (its
    // interpolated world, at its command's view tick): the killcam's view of the kill must have it at the crosshair.
    bool aimAtVictim = false;
    net.MakeCommand = [&]( uint32_t InTick ) {
        InputCommand command;
        if( killerPeer && net.Making == &killerPeer->Net && aimAtVictim )
        {
            EntityHandle self = find( *killerPeer->GameWorld, kKillerGuid );
            EntityHandle target = find( *killerPeer->GameWorld, kVictimGuid );
            const Vector3 to = target->GetComponent<Transform>().GetWorldPosition() - ( self->GetComponent<Transform>().GetWorldPosition() + Vector3( 0.f, kEyeHeight, 0.f ) );
            command.HasAim = true;
            command.Yaw = std::atan2( to.x, to.z ) * 180.f / 3.14159265f;
            command.Pitch = std::asin( std::clamp( to.y / std::max( to.Length(), 1e-4f ), -1.f, 1.f ) ) * 180.f / 3.14159265f;
            return command;
        }
        if( killerPeer && net.Making == &killerPeer->Net )
        {
            command.Axes[0] = 0.3f * std::sin( InTick * 0.02f );
            command.Axes[1] = 0.3f * std::cos( InTick * 0.02f );
            command.HasAim = true;
            command.Yaw = static_cast<float>( ( InTick * 3 ) % 360 );
            command.Pitch = -4.f;
            return command;
        }
        switch( ( InTick / 120 ) % 4 )
        {
        case 0: command.Axes[1] = 0.5f; break;
        case 1: command.Axes[0] = 0.5f; break;
        case 2: command.Axes[1] = -0.5f; break;
        default: command.Axes[0] = -0.5f; break;
        }
        return command;
    };
    std::vector<std::shared_ptr<const NetReplayTick>> ringAtKill;
    std::map<uint32_t, NetReplayEntity> ringEntitiesAtKill;
    net.OnServerTick = [&]( NetServer& InServer ) {
        const float t = static_cast<float>( InServer.GetTick() ) / 60.f;
        for( size_t i = 0; i < movers.size(); ++i )
        {
            movers[i]->GetComponent<Transform>().SetPosition( Vector3( static_cast<float>( i ) * 8.f - 96.f, 0.f, std::sin( t + static_cast<float>( i ) ) * 4.f ) );
        }
        if( killTick != 0 && InServer.GetTick() == killTick )
        {
            REQUIRE( net.ServerNet.SendKillcam( victim, killer, 8.0 ) );
        }
        if( killTick != 0 && ringAtKill.empty() && InServer.GetTick() == killTick + 1 )
        {
            const NetReplayRecorder& recorder = *net.ServerNet.GetRecorder();
            ringAtKill.assign( recorder.GetRing().begin(), recorder.GetRing().end() );
            ringEntitiesAtKill = recorder.GetRingEntities();
        }
        Step( serverPhysics, *net.ServerWorld );
    };
    net.OnClientTick = [&]( NetCore& ) { Step( clientPhysics, *net.ClientWorld ); };

    // The victim's killcam player: each replay world gets the scene's placed pawns and a camera.
    KillcamPlayer player;
    player.GetSettings().HoldSeconds = 0.5;
    int populated = 0;
    // The killer's body: a compact Model on a child of its pawn (it draws from the Model, not Meshes).
    auto addBody = [&]( World& InWorld ) {
        EntityHandle body = InWorld.CreateEntity( "Killer Body" );
        body->AddComponent<Transform>().SetParent( find( InWorld, kKillerGuid )->GetComponent<Transform>() );
        body->AddComponent<Model>();
        return body;
    };
    EntityHandle liveBody = addBody( *net.ClientWorld );
    EntityHandle replayBody;
    // ME-343 review: 1, the replay world can't be made (no scene); 2, it has a placed networked entity the recording's
    // scene hasn't (an additive scene's), so StartPlayback refuses every stream.
    int populateMode = 0;
    player.SetPopulate( [&]( KillcamPlayer::ReplayWorld& InReplay, std::string& OutError ) {
        ++populated;
        if( populateMode == 1 )
        {
            OutError = "there's no scene file to play it in";
            return false;
        }
        placePawns( *InReplay.Instance, false );
        if( populateMode == 2 )
        {
            EntityHandle extra = InReplay.Instance->CreateEntityWithGUID( 0x7A3E99 );
            extra->AddComponent<Transform>();
            extra->AddComponent<NetIdentity>();
        }
        replayBody = addBody( *InReplay.Instance );
        EntityHandle camera = InReplay.Instance->CreateEntity( "Killcam Camera" );
        camera->AddComponent<Transform>();
        camera->AddComponent<Camera>().SetCurrent();
        return true;
    } );
    std::vector<KillcamEvent> events;
    auto listening = EventSubscription::Create<KillcamEvent>( [&]( const KillcamEvent& InEvent ) { events.push_back( InEvent ); } );
    uint64_t playingFrames = 0;
    double lastShown = -1.0;
    bool shownOnlyRose = true;
    bool beyondArrived = false;
    World* replaySeen = nullptr;
    // ME-343: every frame, until the killer's tick reaches the kill, its view is of the tick shown (streaming too:
    // the playback waits where the killer's last tick in was looking); from there it holds the kill's.
    bool inStep = true;
    bool heldOnKill = true;
    uint64_t inStepFrames = 0;
    net.OnMillisecond = [&]( double InNow ) {
        if( static_cast<int64_t>( InNow * 60.0 ) == static_cast<int64_t>( ( InNow - 0.001 ) * 60.0 ) )
        {
            return;
        }
        player.Update( *net.ClientWorld, 1.0 / 60.0 );
        if( player.IsPlaying() )
        {
            const KillcamPlayer::Status& now = player.GetStatus();
            if( now.KillerTick < now.DeathTick )
            {
                inStep = inStep && now.HasView && std::abs( now.KillerViewTick - now.Tick ) < 1e-6 && now.KillerTick > now.Tick;
                ++inStepFrames;
            }
            else
            {
                heldOnKill = heldOnKill && now.KillerTick == static_cast<double>( now.DeathTick );
            }
            ++playingFrames;
            replaySeen = player.GetWorld();
            const NetReplayPlayback& playback = *player.GetNet()->GetPlayback();
            shownOnlyRose = shownOnlyRose && ( playback.IsPaused() || player.GetStatus().Tick >= lastShown );
            lastShown = player.GetStatus().Tick;
            // Never past what had arrived: the last tick of the chunks in when it moved (OnMillisecond runs
            // before this millisecond's packets, so the source is as the playback last saw it).
            const NetReplayMemorySource& source = *net.ClientNet.GetKillcam().GetSource();
            beyondArrived = beyondArrived || source.GetChunkCount() == 0 || playback.GetTick() > source.GetChunkInfo( source.GetChunkCount() - 1 ).LastTick;
        }
    };

    net.Start();
    for( int i = 0; i < 24; ++i )
    {
        EntityHandle mover = net.ServerWorld->CreateEntity();
        mover->AddComponent<Transform>();
        mover->AddComponent<NetIdentity>();
        movers.push_back( mover );
    }
    net.ServerNet.GetReplicationServer()->Settings.RelevantRadius = 30.f;
    REQUIRE( net.ServerNet.StartRecording( "" ) );
    net.ClientNet.Predict( clientPawn );
    net.Run( 1.5 );
    Peer& peer = net.AddClient( Link( 40.f, 2.f, 0.f, 3233 ), [&]( World& InWorld ) {
        floor( InWorld );
        placePawns( InWorld, false );
        InWorld.Simulate();
        InWorld.Start();
    } );
    killerPeer = &peer;
    net.Run( 1.5 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    REQUIRE( peer.Net.GetClient()->GetClientId() == 2 );
    killer->GetComponent<NetIdentity>().SetOwner( 2 );
    net.Run( 9.0 );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( populated == 0 );
    // ME-343: the replay world is built ahead of the first death (Prepare: the engine does it once the scene's read
    // ahead), off screen until a killcam plays, and kept for every killcam after.
    REQUIRE( player.Prepare( *net.ClientWorld ) );
    CHECK( populated == 1 );
    CHECK( player.Prepare( *net.ClientWorld ) );
    CHECK( populated == 1 );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( player.GetWorld() == nullptr );
    World* const kept = player.GetKeptWorld();
    REQUIRE( kept != nullptr );
    CHECK( kept->IsReplayWorld );
    const size_t builtEntities = kept->GetEntityCount();
    CHECK( player.GetStats().WorldsBuilt == 1 );

    NetPredictor& predictor = *net.ClientNet.GetPredictor();
    NetServer::Client& onServer = *net.ServerNet.GetServer()->GetClients().front();
    REQUIRE( onServer.Id == 1 );
    struct Counts
    {
        uint64_t Snapshots = 0;
        uint64_t Misses = 0;
        int Corrections = 0;
        int Resyncs = 0;
        uint32_t Tick = 0;
        double At = 0.0;
    };
    auto count = [&]() {
        Counts counts;
        counts.Snapshots = net.ClientNet.GetReplicationClient()->GetSnapshotsReceived();
        counts.Misses = onServer.Commands.GetMissedCount();
        counts.Corrections = predictor.GetCorrectionCount();
        counts.Resyncs = net.ClientNet.GetClient()->GetClock().GetResyncCount();
        counts.Tick = net.ClientNet.GetTick();
        counts.At = net.Now();
        return counts;
    };
    const Counts before = count();
    net.Run( 3.0 );
    aimAtVictim = true;
    net.Run( 1.0 );
    const Counts baseline = count();
    const double baselineRate = static_cast<double>( baseline.Snapshots - before.Snapshots ) / ( baseline.At - before.At );
    const size_t liveEntities = net.ClientWorld->GetEntityCount();

    // The kill. The killcam starts on the victim with its first chunk, then plays at 60 Hz while the rest
    // stream in; 2 s into it, still playing.
    killTick = net.ServerNet.GetServer()->GetTick() + 2;
    double startedAt = -1.0;
    double completeAt = -1.0;
    for( int i = 0; i < 1000 && !( player.IsPlaying() && player.GetStatus().Tick >= player.GetStatus().FirstTick + 120.0 ); ++i )
    {
        net.Run( 0.01 );
        if( startedAt < 0.0 && player.IsPlaying() )
        {
            startedAt = net.Now();
            // It started on what had arrived: its first chunk, perhaps not the whole stream.
            MESSAGE( "the killcam started with " << net.ClientNet.GetKillcam().GetChunksReady() << " of " << net.ClientNet.GetKillcam().GetChunkCount() << " chunks in" );
        }
        if( completeAt < 0.0 && net.ClientNet.GetKillcam().IsComplete() )
        {
            completeAt = net.Now();
        }
    }
    REQUIRE( player.IsPlaying() );
    REQUIRE( startedAt > 0.0 );
    CHECK( populated == 1 );
    CHECK( player.GetWorld() == kept );
    CHECK( player.GetStatus().WorldKept );
    CHECK( completeAt > startedAt );        // it played before the stream was whole
    const KillcamPlayer::Status status = player.GetStatus();
    CHECK( status.Id == 1 );
    CHECK( status.DeathTick == killTick );
    CHECK( status.FirstTick == killTick - 479 );
    CHECK( status.KillerClient == 2 );
    CHECK( status.KillerNetId == killer->GetComponent<NetIdentity>().GetNetId() );
    CHECK( status.VictimNetId == victim->GetComponent<NetIdentity>().GetNetId() );
    CHECK( std::abs( status.Duration - ( 479.0 / 60.0 + 0.5 ) ) < 1e-9 );
    CHECK( status.Elapsed > 1.9 );
    CHECK( shownOnlyRose );
    CHECK_FALSE( beyondArrived );
    CHECK( inStep );
    CHECK( inStepFrames > 100 );
    MESSAGE( player.GetStats().StalledFrames << " frames held for the stream (" << inStepFrames << " with the killer's view in step)" );
    REQUIRE( events.size() == 1 );
    CHECK( events[0].Type == KillcamEvent::Kind::Started );
    CHECK( events[0].Source == net.ClientWorld.get() );
    CHECK( events[0].KillerNetId == status.KillerNetId );

    // Two worlds: the replay world has its own entities (its own killer, victim, movers), the live world
    // gained none, each has its own SingletonKillcam pointing at the player.
    World& replay = *player.GetWorld();
    REQUIRE( &replay != net.ClientWorld.get() );
    CHECK( replay.IsReplayWorld );
    CHECK_FALSE( net.ClientWorld->IsReplayWorld );
    CHECK( net.ClientWorld->GetEntityCount() == liveEntities );
    NetCore& replayNet = *player.GetNet();
    EntityHandle replayKiller = replayNet.FindEntity( status.KillerNetId );
    EntityHandle liveKiller = net.ClientNet.FindEntity( status.KillerNetId );
    REQUIRE( replayKiller );
    REQUIRE( liveKiller );
    CHECK( replayKiller.GetWorld() == &replay );
    CHECK( liveKiller.GetWorld() == net.ClientWorld.get() );
    CHECK( replay.Singleton<SingletonKillcam>().Player == &player );
    CHECK( replay.Singleton<SingletonKillcam>().InReplayWorld );
    CHECK( net.ClientWorld->Singleton<SingletonKillcam>().Player == &player );
    CHECK_FALSE( net.ClientWorld->Singleton<SingletonKillcam>().InReplayWorld );
    CHECK_FALSE( replayNet.IsSessionLive() );
    CHECK( replayNet.IsPlayback() );
    // ME-343: each world its own id, each world's session its own (C# keys Scripts.All and Net.Events by them).
    CHECK( replay.GetId() != net.ClientWorld->GetId() );
    CHECK( replay.GetId() != 0 );
    const uint32_t firstReplaySession = replayNet.GetSessionId();
    CHECK( firstReplaySession != 0 );
    CHECK( firstReplaySession != net.ClientNet.GetSessionId() );
    CHECK( net.ClientNet.GetSessionId() != net.ServerNet.GetSessionId() );
#if USING( ME_SCRIPTING )
    // C#'s Net.Killcam: in the live world, one plays; in the replay world, one plays and it's that world.
    // Killer and Victim are each world's own entities.
    ScriptEngineAPI api{};
    Register_NetBindings( api );
    {
        int id = 0, killerId = 0, victimId = 0, firstTick = 0, deathTick = 0;
        float tick = 0.f, elapsed = 0.f, duration = 0.f;
        {
            ScriptBindings::WorldScope scope( net.ClientWorld );
            CHECK( api.Net_GetKillcamPlayback( &id, &killerId, &victimId, &tick, &firstTick, &deathTick, &elapsed, &duration ) == 1 );
        }
        CHECK( id == 1 );
        CHECK( killerId == static_cast<int>( status.KillerNetId ) );
        CHECK( victimId == static_cast<int>( status.VictimNetId ) );
        CHECK( firstTick == static_cast<int>( status.FirstTick ) );
        CHECK( deathTick == static_cast<int>( status.DeathTick ) );
        CHECK( std::abs( tick - static_cast<float>( status.Tick ) ) < 0.01f );
        CHECK( std::abs( elapsed - static_cast<float>( status.Elapsed ) ) < 1e-3f );
        CHECK( std::abs( duration - static_cast<float>( status.Duration ) ) < 1e-3f );
        int idHere = 0;
        float progress = 0.f;
        EntityID killerHere;
        EntityID victimHere;
        {
            ScriptBindings::WorldScope scope( player.GetWorldPtr() );
            CHECK( api.Net_GetKillcamPlayback( &id, &killerId, &victimId, &tick, &firstTick, &deathTick, &elapsed, &duration ) == 3 );
            CHECK( api.Net_GetKillcam( &idHere, &progress, &killerHere, &victimHere ) );
            CHECK( api.Net_GetRole() == 2 );    // the replay world presents like a client
            CHECK( api.Net_GetSessionId() == static_cast<int>( firstReplaySession ) );
        }
        {
            ScriptBindings::WorldScope scope( net.ClientWorld );
            CHECK( api.Net_GetSessionId() == static_cast<int>( net.ClientNet.GetSessionId() ) );
        }
        CHECK( idHere == 1 );
        CHECK( killerHere == replayKiller->GetId() );
        CHECK( victimHere == replayNet.FindEntity( status.VictimNetId )->GetId() );
        {
            ScriptBindings::WorldScope scope( net.ServerWorld );
            CHECK( api.Net_GetKillcamPlayback( &id, &killerId, &victimId, &tick, &firstTick, &deathTick, &elapsed, &duration ) == 0 );
            CHECK( id == 0 );
        }
    }
#endif

    // The killer's view (ME-343): the replay world's camera at its eye (the game's 1.5 m) looking along its client's recorded aim, both from the killer's tick that was looking at the tick shown:
    // the one whose command's view tick (the others on its screen, its interpolation delay behind) reaches it.
    REQUIRE( status.HasView );
    {
        NetReplayReader& reader = replayNet.GetPlayback()->GetReader();
        const uint32_t before = static_cast<uint32_t>( std::floor( status.KillerTick ) );
        NetReplayTick from, to;
        REQUIRE( reader.ReadTick( before, from ) );
        REQUIRE( reader.ReadTick( before + 1, to ) );
        const NetReplayClient* fromClient = from.Clients.empty() ? nullptr : &from.Clients[0];
        const NetReplayClient* toClient = to.Clients.empty() ? nullptr : &to.Clients[0];
        REQUIRE( fromClient );
        REQUIRE( toClient );
        INFO( "tick shown " << status.Tick << ", the killer's " << status.KillerTick << " (view ticks " << fromClient->Command.ViewTick << " .. " << toClient->Command.ViewTick << ")" );
        // Ahead of the tick shown by the killer's interpolation delay (150 ms RTT: several ticks), and
        // between the two ticks whose view ticks are either side of it.
        CHECK( status.KillerTick > status.Tick + 2.0 );
        CHECK( status.KillerTick < status.Tick + 30.0 );
        CHECK( fromClient->Command.ViewTick <= status.Tick + 1e-6 );
        CHECK( toClient->Command.ViewTick >= status.Tick - 1e-6 );
        // The eye at the killer's recorded position then, not its pawn's in the world shown.
        const float alpha = static_cast<float>( status.KillerTick - before );
        const Vector3 fromAt = from.State.Entities.at( status.KillerNetId ).GetPosition();
        const Vector3 toAt = to.State.Entities.at( status.KillerNetId ).GetPosition();
        const Vector3 killerAt = fromAt + ( toAt - fromAt ) * alpha;
        CHECK( ( status.KillerAt - killerAt ).Length() < 1e-3f );
        CHECK( ( status.Eye - ( killerAt + Vector3( 0.f, kEyeHeight, 0.f ) ) ).Length() < 1e-3f );
        CHECK( std::abs( status.Pitch - ( fromClient->Command.Pitch + ( toClient->Command.Pitch - fromClient->Command.Pitch ) * alpha ) ) < 0.01f );
        float delta = std::fmod( std::abs( status.Yaw - fromClient->Command.Yaw ), 360.f );
        delta = std::min( delta, 360.f - delta );
        CHECK( delta <= 3.01f );    // between that tick's aim and the next's (it turns 3 degrees a tick)
    }
    Camera* view = Camera::GetMain( &replay );
    REQUIRE( view );
    REQUIRE( view->Parent );
    CHECK( ( view->Parent->GetComponent<Transform>().GetWorldPosition() - status.Eye ).Length() < 1e-4f );
    CHECK( Camera::GetMain( net.ClientWorld.get() ) != view );
    {
        // Looking along the aim it reports.
        const float yaw = status.Yaw * 3.14159265f / 180.f;
        const float pitch = status.Pitch * 3.14159265f / 180.f;
        const Vector3 aim( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
        const Vector3 front = view->Parent->GetComponent<Transform>().Front().Normalized();
        INFO( "front " << front.x << ", " << front.y << ", " << front.z << "; aim " << aim.x << ", " << aim.y << ", " << aim.z );
        CHECK( ( front - aim ).Length() < 1e-3f );
    }
    // Its own body hidden in the replay world (a compact Model too), the live world's left alone.
    REQUIRE( replayBody );
    CHECK( replayBody.GetWorld() == &replay );
    CHECK_FALSE( replayBody->GetComponent<Model>().IsEnabled() );
    CHECK( liveBody->GetComponent<Model>().IsEnabled() );

    // Tick for tick, the replay world is the ring as the killer's client had it: walk the playback over
    // every tick of the killcam (the live session waits, it's the same instant) and check the world at
    // each against the stream, and the stream against the ring.
    REQUIRE( !ringAtKill.empty() );
    REQUIRE( net.ClientNet.GetKillcam().IsComplete() );
    NetReplayPlayback& playback = *replayNet.GetPlayback();
    playback.Pause();
    playback.Seek( status.FirstTick );
    const uint32_t killerId = status.KillerNetId;
    const uint32_t victimId = status.VictimNetId;
    std::map<uint32_t, uint8_t> owners;
    for( const auto& [id, entity] : ringEntitiesAtKill )
    {
        owners[id] = entity.Owner;
    }
    std::map<uint32_t, NetEntityState> held;
    uint32_t walked = 0;
    size_t statesChecked = 0;
    bool killSeen = false;
    for( size_t i = 0; i < ringAtKill.size(); ++i )
    {
        const NetReplayTick& original = *ringAtKill[i];
        if( i > 0 )
        {
            for( const NetReplayEntity& entity : original.Entities )
            {
                owners[entity.NetId] = entity.Owner;
            }
        }
        const std::set<uint32_t> relevant = Relevant( original, owners, 2, 30.f, {}, original.State.Entities.at( killerId ).GetPosition(), killerId, victimId );
        Hold( held, original, relevant );
        if( original.Tick < status.FirstTick )
        {
            continue;
        }
        INFO( "tick " << original.Tick );
        if( original.Tick > status.FirstTick )
        {
            playback.Step( 1 );
        }
        player.Update( *net.ClientWorld, 0.0 );
        REQUIRE( player.IsPlaying() );
        REQUIRE( playback.GetTick() == original.Tick );
        const NetReplayPlayback::Check check = replayNet.CheckPlayback();
        for( const std::string& problem : check.Problems )
        {
            FAIL_CHECK( problem );
        }
        CHECK( check.Passed() );
        REQUIRE( playback.GetRecord() );
        CHECK( playback.GetRecord()->State.Entities == held );
        statesChecked += check.Matched;
        ++walked;
        // The killer's view of the kill (ME-343), the first tick it's there: the victim at the crosshair, where the
        // killer aimed at it through its interpolation; the eye and aim the killer's own shot's.
        const KillcamPlayer::Status& now = player.GetStatus();
        if( !killSeen && now.KillerTick >= now.DeathTick )
        {
            killSeen = true;
            MESSAGE( "the killer's view of the kill at tick " << now.Tick << ": " << player.DescribeView() );
            CHECK( now.KillerTick == static_cast<double>( now.DeathTick ) );
            CHECK( now.Tick < now.DeathTick - 2.0 );     // the world as the killer saw it: its interpolation delay before the death
            REQUIRE( now.HasView );
            REQUIRE( now.HasVictim );
            CHECK( now.VictimView.Inside );
            CHECK( now.VictimView.Depth > 5.f );
            CHECK( std::abs( now.VictimView.X ) < 0.1f );
            CHECK( std::abs( now.VictimView.Y ) < 0.1f );
            CHECK( player.GetStats().DeathViews == 1 );
            REQUIRE( now.HasShot );
            CHECK( std::abs( now.Eye.y - now.ShotOrigin.y ) < 0.01f );
            CHECK( ( now.Eye - now.ShotOrigin ).Length() < 0.05f );
            CHECK( now.ShotDirection.Dot( KillcamPlayer::AimDirection( now.Yaw, now.Pitch ) ) > 0.99999f );
        }
    }
    CHECK( killSeen );
    CHECK( player.GetStatus().KillerTick == static_cast<double>( player.GetStatus().DeathTick ) );
    CHECK( player.GetStats().DeathViews == 1 );
    MESSAGE( walked << " ticks walked, " << statesChecked << " entity states in the replay world checked against the ring; " << player.GetStats().StalledFrames << " frames waited on a chunk" );
    CHECK( walked == 480 );
    CHECK( statesChecked > 480 * 20 );

    // On the death tick it holds, then ends: the replay world goes at once. The kill's view held to the end.
    heldOnKill = true;
    for( int i = 0; i < 200 && player.IsPlaying(); ++i )
    {
        net.Run( 0.01 );
    }
    CHECK( heldOnKill );
    CHECK( player.GetStats().DeathViews == 1 );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( player.GetWorld() == nullptr );
    // Kept (ME-343), as it was built: the killcam's spawns (24 movers) gone, the placed killer's body shown again.
    CHECK( player.GetKeptWorld() == kept );
    CHECK( kept->GetEntityCount() == builtEntities );
    CHECK( replayBody->GetComponent<Model>().IsEnabled() );
    CHECK( player.GetStats().WorldsDiscarded == 0 );
    REQUIRE( events.size() == 2 );
    CHECK( events[1].Type == KillcamEvent::Kind::Ended );
    CHECK( events[1].Reason == "played" );
    CHECK( events[1].Id == 1 );
    CHECK( player.GetStats().Played == 1 );
    CHECK( player.GetStats().Failed == 0 );
    CHECK( net.ClientWorld->Singleton<SingletonKillcam>().Player == &player );

    // The live session meanwhile, from the kill to the end of the killcam: the snapshot rate, the clock,
    // the prediction and the commands as they were without one; the live world still predicting its
    // pawn, ticking on.
    const Counts during = count();
    const double seconds = during.At - baseline.At;
    const double duringRate = static_cast<double>( during.Snapshots - baseline.Snapshots ) / seconds;
    MESSAGE( "live snapshots " << baselineRate << "/s before, " << duringRate << "/s while the killcam played (" << playingFrames << " frames of it); corrections "
                                << baseline.Corrections - before.Corrections << " before, " << during.Corrections - baseline.Corrections << " during" );
    CHECK( baselineRate > 50.0 );
    CHECK( std::abs( duringRate - baselineRate ) <= baselineRate * 0.05 );
    CHECK( during.Corrections == baseline.Corrections );
    CHECK( during.Resyncs == baseline.Resyncs );
    CHECK( static_cast<double>( during.Misses - baseline.Misses ) / seconds <= static_cast<double>( baseline.Misses - before.Misses ) / 4.0 + 0.5 );
    CHECK( during.Tick > baseline.Tick + static_cast<uint32_t>( seconds * 60.0 * 0.9 ) );
    CHECK( net.ClientNet.GetPredictor()->GetPawn() == clientPawn );
    CHECK( net.ClientWorld->GetEntityCount() == liveEntities );
    CHECK( replaySeen != nullptr );

    // Two more kills a second apart: the second killcam starts, the third replaces it as soon as its first
    // chunk is in; then C#'s Net.Killcam.Skip from the replay world's script ends it at the next update
    // (not while that world's scripts run), and the live world is presented again.
    killTick = net.ServerNet.GetServer()->GetTick() + 2;
    ringAtKill.clear();
    for( int i = 0; i < 300 && !( player.IsPlaying() && player.GetStatus().Id == 2 ); ++i )
    {
        net.Run( 0.01 );
    }
    REQUIRE( player.IsPlaying() );
    CHECK( player.GetStatus().Id == 2 );
    CHECK( player.GetWorld() == kept );
    // The kept world's next killcam is a session of its own (ME-343: C#'s Net.Events starts empty for it).
    CHECK( player.GetNet()->GetSessionId() != firstReplaySession );
    CHECK( player.GetNet()->GetSessionId() != 0 );
    net.Run( 0.5 );
    killTick = net.ServerNet.GetServer()->GetTick() + 2;
    ringAtKill.clear();
    for( int i = 0; i < 300 && !( player.IsPlaying() && player.GetStatus().Id == 3 ); ++i )
    {
        net.Run( 0.01 );
    }
    REQUIRE( player.IsPlaying() );
    CHECK( player.GetStatus().Id == 3 );
    CHECK( player.GetStats().Replaced == 1 );
    REQUIRE( events.size() == 5 );
    CHECK( events[2].Type == KillcamEvent::Kind::Started );
    CHECK( events[3].Type == KillcamEvent::Kind::Ended );
    CHECK( events[3].Reason == "replaced" );
    CHECK( events[3].Id == 2 );
    CHECK( events[4].Type == KillcamEvent::Kind::Started );
    CHECK( events[4].Id == 3 );
    // The replaced one's world reset and played the next straight away (ME-343).
    CHECK( player.GetWorld() == kept );
    CHECK( player.GetStatus().WorldKept );
    // Skipped while it still streams (ME-343): the download stops. The receiver takes no more of it and the
    // server, told, cancels its blobs on their way and queued.
    const NetKillcamReceiver& arriving = net.ClientNet.GetKillcam();
    REQUIRE( arriving.GetId() == 3 );
    REQUIRE_FALSE( arriving.IsComplete() );
    REQUIRE( net.ServerNet.GetKillcamsPending() == 1 );
    const uint64_t cancelledBefore = net.ServerNet.GetKillcamStats().CancelledByClient;
#if USING( ME_SCRIPTING )
    {
        ScriptBindings::WorldScope scope( player.GetWorldPtr() );
        api.Net_SkipKillcam();
    }
#else
    player.Skip();
#endif
    CHECK( player.IsPlaying() );    // until the next update
    player.Update( *net.ClientWorld, 1.0 / 60.0 );
    CHECK_FALSE( player.IsPlaying() );
    REQUIRE( events.size() == 6 );
    CHECK( events[5].Reason == "skipped" );
    CHECK( player.GetStats().Skipped == 1 );
    CHECK( player.GetStats().Started == 3 );
    CHECK( arriving.IsCancelled() );
    const uint32_t chunksAtSkip = arriving.GetChunksReceived();
    MESSAGE( "killcam 3 skipped with " << chunksAtSkip << " of " << arriving.GetChunkCount() << " chunks in" );
    // Built once, kept for all three (ME-343).
    CHECK( populated == 1 );
    CHECK( player.GetStats().WorldsBuilt == 1 );
    CHECK( player.GetStats().WorldsReused == 3 );
    CHECK( player.GetKeptWorld() == kept );
    CHECK( kept->GetEntityCount() == builtEntities );
    // Nothing more to play: the newest has been seen. Within a round trip the server has dropped the rest of it
    // and the link carries none of it after that: no chunk more, no blob of it on its way either side.
    net.Run( 0.3 );
    CHECK( net.ServerNet.GetKillcamStats().CancelledByClient == cancelledBefore + 1 );
    CHECK( net.ServerNet.GetKillcamsPending() == 0 );
    CHECK( onServer.Connection->GetBlobProgress().empty() );
    const uint32_t chunksAfterCancel = arriving.GetChunksReceived();
    net.Run( 1.5 );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( player.GetStats().Started == 3 );
    CHECK_FALSE( arriving.IsComplete() );
    CHECK( arriving.GetChunksReceived() == chunksAfterCancel );
    CHECK( arriving.GetChunksReceived() < arriving.GetChunkCount() );
    CHECK( net.ClientNet.GetClient()->GetConnection().GetBlobProgress().empty() );

    // ME-343: a player that doesn't keep its world (Settings::KeepWorld off) builds one per killcam and lets it go.
    player.GetSettings().KeepWorld = false;
    player.Release();
    CHECK( player.GetKeptWorld() == nullptr );
    CHECK_FALSE( player.Prepare( *net.ClientWorld ) );
    killTick = net.ServerNet.GetServer()->GetTick() + 2;
    ringAtKill.clear();
    for( int i = 0; i < 300 && !( player.IsPlaying() && player.GetStatus().Id == 4 ); ++i )
    {
        net.Run( 0.01 );
    }
    REQUIRE( player.IsPlaying() );
    CHECK_FALSE( player.GetStatus().WorldKept );
    CHECK( populated == 2 );
    player.Skip();
    player.Update( *net.ClientWorld, 1.0 / 60.0 );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( player.GetKeptWorld() == nullptr );
    CHECK( player.GetStats().WorldsBuilt == 2 );

    // ME-343 review. Each kill from here: the server sends killcam InId; true once the player has looked at it.
    player.GetSettings().KeepWorld = true;
    auto kill = [&]( uint32_t InId ) {
        killTick = net.ServerNet.GetServer()->GetTick() + 2;
        ringAtKill.clear();
        for( int i = 0; i < 300 && player.GetLastSeenId() != InId; ++i )
        {
            net.Run( 0.01 );
        }
        return player.GetLastSeenId() == InId;
    };
    const KillcamPlayer::Stats& stats = player.GetStats();

    // Skip with none playing lets the newest go before it plays (its first chunk in, AutoPlay off here; on a slow
    // link, not in yet): it never plays and its download stops. With nothing new to skip, Skip does nothing.
    player.GetSettings().AutoPlay = false;
    killTick = net.ServerNet.GetServer()->GetTick() + 2;
    ringAtKill.clear();
    for( int i = 0; i < 300 && !( arriving.GetId() == 5 && arriving.GetChunksReady() > 0 ); ++i )
    {
        net.Run( 0.01 );
    }
    REQUIRE( arriving.GetId() == 5 );
    REQUIRE_FALSE( arriving.IsComplete() );
    CHECK( player.GetLastSeenId() != 5 );
    uint64_t cancelledSoFar = net.ServerNet.GetKillcamStats().CancelledByClient;
    player.Skip();
    player.Update( *net.ClientWorld, 1.0 / 60.0 );
    CHECK( stats.SkippedUnplayed == 1 );
    CHECK( player.GetLastSeenId() == 5 );
    CHECK( arriving.IsCancelled() );
    player.Skip();
    player.Update( *net.ClientWorld, 1.0 / 60.0 );
    CHECK( stats.SkippedUnplayed == 1 );
    player.GetSettings().AutoPlay = true;
    net.Run( 0.5 );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( stats.Started == 4 );
    CHECK( net.ServerNet.GetKillcamStats().CancelledByClient == cancelledSoFar + 1 );
    CHECK_FALSE( arriving.IsComplete() );

    // A killcam that can't start (its world can't be made) stops its download too, and is never tried again; Prepare
    // tries once, so no frame of live play builds after it.
    populateMode = 1;
    player.Release();
    int populatedSoFar = populated;
    CHECK_FALSE( player.Prepare( *net.ClientWorld ) );
    CHECK_FALSE( player.Prepare( *net.ClientWorld ) );
    CHECK( populated == populatedSoFar + 1 );
    uint64_t failedSoFar = stats.Failed;
    cancelledSoFar = net.ServerNet.GetKillcamStats().CancelledByClient;
    REQUIRE( kill( 6 ) );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( stats.Failed == failedSoFar + 1 );
    CHECK( populated == populatedSoFar + 2 );
    CHECK( arriving.IsCancelled() );
    net.Run( 0.3 );
    CHECK( net.ServerNet.GetKillcamStats().CancelledByClient == cancelledSoFar + 1 );
    CHECK_FALSE( player.Prepare( *net.ClientWorld ) );
    CHECK( populated == populatedSoFar + 2 );
    CHECK( stats.Started == 4 );

    // A stream no world fits (StartPlayback refuses it: the world has a placed networked entity the recording hasn't).
    // The kept world refuses it, so one is built afresh (WorldsDiscarded); that refuses it too, so the streams are
    // what don't fit: it's kept, the next killcam builds nothing, and both stop their downloads.
    populateMode = 2;
    player.Release();
    populatedSoFar = populated;
    const uint64_t builtSoFar = stats.WorldsBuilt;
    const uint64_t discardedSoFar = stats.WorldsDiscarded;
    REQUIRE( player.Prepare( *net.ClientWorld ) );
    failedSoFar = stats.Failed;
    cancelledSoFar = net.ServerNet.GetKillcamStats().CancelledByClient;
    REQUIRE( kill( 7 ) );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( stats.Failed == failedSoFar + 1 );
    CHECK( stats.WorldsDiscarded == discardedSoFar + 1 );
    CHECK( stats.WorldsBuilt == builtSoFar + 2 );
    CHECK( populated == populatedSoFar + 2 );
    CHECK( arriving.IsCancelled() );
    REQUIRE( player.GetKeptWorld() != nullptr );
    CHECK( player.Prepare( *net.ClientWorld ) );
    CHECK( populated == populatedSoFar + 2 );
    net.Run( 0.3 );
    CHECK( net.ServerNet.GetKillcamStats().CancelledByClient == cancelledSoFar + 1 );
    REQUIRE( kill( 8 ) );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( stats.Failed == failedSoFar + 2 );
    CHECK( stats.WorldsDiscarded == discardedSoFar + 1 );
    CHECK( stats.WorldsBuilt == builtSoFar + 2 );
    CHECK( populated == populatedSoFar + 2 );
    CHECK( arriving.IsCancelled() );
    net.Run( 0.3 );
    CHECK( net.ServerNet.GetKillcamStats().CancelledByClient == cancelledSoFar + 2 );
    CHECK( stats.Started == 4 );

    // A kept world that loses an entity it was built with during a killcam (here the killer's body; a placed networked
    // entity the stream destroys alike) can't be made whole: it's thrown away as the killcam ends, Prepare doesn't
    // build it again in live play, and the next killcam builds one as built.
    populateMode = 0;
    player.Release();
    REQUIRE( player.Prepare( *net.ClientWorld ) );
    World* const second = player.GetKeptWorld();
    REQUIRE( second != nullptr );
    const size_t secondEntities = second->GetEntityCount();
    const uint64_t builtBefore = stats.WorldsBuilt;
    const uint64_t discardedBefore = stats.WorldsDiscarded;
    REQUIRE( kill( 9 ) );
    REQUIRE( player.IsPlaying() );
    CHECK( player.GetStatus().WorldKept );
    REQUIRE( replayBody );
    second->DestroyEntity( replayBody );
    net.Run( 0.05 );
    REQUIRE( player.IsPlaying() );
    player.Skip();
    player.Update( *net.ClientWorld, 1.0 / 60.0 );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( stats.WorldsDiscarded == discardedBefore + 1 );
    CHECK( player.GetKeptWorld() == nullptr );
    populatedSoFar = populated;
    CHECK_FALSE( player.Prepare( *net.ClientWorld ) );
    CHECK( populated == populatedSoFar );
    REQUIRE( kill( 10 ) );
    REQUIRE( player.IsPlaying() );
    CHECK_FALSE( player.GetStatus().WorldKept );
    CHECK( stats.WorldsBuilt == builtBefore + 1 );
    CHECK( populated == populatedSoFar + 1 );
    player.Skip();
    player.Update( *net.ClientWorld, 1.0 / 60.0 );
    CHECK_FALSE( player.IsPlaying() );
    REQUIRE( player.GetKeptWorld() != nullptr );
    CHECK( player.GetKeptWorld()->GetEntityCount() == secondEntities );
}


TEST_CASE( "Net: a killcam that outruns its stream holds on the last tick in, still playing, and goes on when the next chunk lands; one whose stream stops ends (failed) after MaxStallSeconds; a player's waits where the killer's last tick in was looking (ME-343); a session's end starts the killcam ids over (ME-323 phase 3)" )
{
    // A bot kills client 1's pawn. Killcams of 3 s in chunks of 10 ticks: the first goes alone, the rest
    // once it's acked, so cutting the server -> client link as the victim starts playing leaves it 10
    // ticks to play before it runs out of stream.
    Harness net( Link( 50.f, 0.f, 0.f, 3241 ), Link( 50.f, 0.f, 0.f, 3242 ) );
    constexpr uint64_t kVictimGuid = 0x7A3E21;
    net.Place( kVictimGuid );
    EntityHandle victim;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == kVictimGuid )
        {
            victim = net.ServerWorld->GetEntity( InEntity.GetId() );
        }
    } );
    REQUIRE( victim );
    victim->GetComponent<Transform>().SetPosition( Vector3( 5.f, 0.f, 0.f ) );
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        world->Simulate();
        world->Start();
    }
    EntityHandle killer;
    net.OnServerTick = [&]( NetServer& InServer ) {
        if( killer )
        {
            const float t = static_cast<float>( InServer.GetTick() ) / 60.f;
            killer->GetComponent<Transform>().SetPosition( Vector3( std::sin( t ) * 3.f, 0.f, std::cos( t ) * 3.f ) );
        }
        net.ServerWorld->Simulate();
    };
    NetCore::KillcamSettings chunking;
    chunking.ChunkTicks = 10;
    net.ServerNet.SetKillcamSettings( chunking );
    net.Start();
    net.Run( 1.0 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    victim->GetComponent<NetIdentity>().SetOwner( 1 );
    REQUIRE( net.ServerNet.StartRecording( "" ) );
    killer = net.ServerWorld->CreateEntity();
    killer->SetName( "Bot" );
    killer->AddComponent<Transform>();
    killer->AddComponent<NetIdentity>();
    net.Run( 4.0 );

    KillcamPlayer player;
    player.GetSettings().HoldSeconds = 0.25;
    player.SetPopulate( [&]( KillcamPlayer::ReplayWorld& InReplay, std::string& ) {
        EntityHandle placed = InReplay.Instance->CreateEntityWithGUID( kVictimGuid );
        placed->AddComponent<Transform>();
        placed->AddComponent<NetIdentity>();
        return true;
    } );
    std::vector<KillcamEvent> events;
    auto listening = EventSubscription::Create<KillcamEvent>( [&]( const KillcamEvent& InEvent ) { events.push_back( InEvent ); } );
    const NetConditions open = Link( 50.f, 0.f, 0.f, 3243 );
    const NetConditions cut = Link( 50.f, 0.f, 1.f, 3244 );
    bool cutOnStart = false;
    double cutAt = -1.0;
    // While it plays, each frame: never past the last tick in; and each frame it waited on a chunk, held
    // on exactly that tick, still playing (not paused), the replay world the recording's there.
    uint64_t stalledSeen = 0;
    int heldFrames = 0;
    bool heldRight = true;
    bool beyondArrived = false;
    size_t heldMatched = 0;
    std::vector<std::string> heldProblems;
    uint32_t heldAt = 0;
    double shownAfterHeld = 0.0;
    // A player's killcam (step 3, ME-343): each frame held, at the view tick of the killer's last tick in (before
    // that tick: its view of anything later hasn't arrived), and every frame before the kill, the killer's view of
    // the tick shown.
    int heldPlayerFrames = 0;
    bool heldInView = true;
    bool playerInStep = true;
    uint64_t playerFrames = 0;
    net.OnMillisecond = [&]( double InNow ) {
        if( static_cast<int64_t>( InNow * 60.0 ) == static_cast<int64_t>( ( InNow - 0.001 ) * 60.0 ) )
        {
            return;
        }
        player.Update( *net.ClientWorld, 1.0 / 60.0 );
        if( !player.IsPlaying() )
        {
            return;
        }
        if( cutOnStart && cutAt < 0.0 )
        {
            net.ServerLink.SetConditions( cut );
            cutAt = InNow;
        }
        NetReplayPlayback& playback = *player.GetNet()->GetPlayback();
        const NetReplayMemorySource& source = *net.ClientNet.GetKillcam().GetSource();
        const uint32_t readable = source.GetChunkInfo( source.GetChunkCount() - 1 ).LastTick;
        beyondArrived = beyondArrived || playback.GetTick() > readable;
        const KillcamPlayer::Status& now = player.GetStatus();
        if( now.KillerClient != 0 )
        {
            if( now.KillerTick < now.DeathTick )
            {
                playerInStep = playerInStep && std::abs( now.KillerViewTick - now.Tick ) < 1e-6;
                ++playerFrames;
            }
            if( player.GetStats().StalledFrames > stalledSeen )
            {
                stalledSeen = player.GetStats().StalledFrames;
                ++heldPlayerFrames;
                heldInView = heldInView && !playback.IsPaused() && playback.IsStreaming() && now.Tick < static_cast<double>( readable ) - 2.0
                    && std::abs( now.Tick - playback.GetHoldAt() ) < 1e-9 && std::abs( now.KillerTick - readable ) < 1e-9 && readable < now.DeathTick;
            }
            return;
        }
        if( player.GetStats().StalledFrames > stalledSeen )
        {
            stalledSeen = player.GetStats().StalledFrames;
            ++heldFrames;
            heldAt = readable;
            heldRight = heldRight && !playback.IsPaused() && playback.IsStreaming() && playback.GetTick() == readable && playback.GetLastTick() == readable
                && player.GetStatus().Tick == static_cast<double>( readable ) && readable < player.GetStatus().DeathTick;
            const NetReplayPlayback::Check check = player.GetNet()->CheckPlayback();
            heldMatched += check.Matched;
            heldProblems.insert( heldProblems.end(), check.Problems.begin(), check.Problems.end() );
        }
        else if( heldAt != 0 )
        {
            shownAfterHeld = std::max( shownAfterHeld, player.GetStatus().Tick );
        }
    };
    auto runUntil = [&]( const std::function<bool()>& InDone, double InSeconds ) {
        for( int i = 0; i < static_cast<int>( InSeconds * 100.0 ) && !InDone(); ++i )
        {
            net.Run( 0.01 );
        }
        return InDone();
    };

    // 1. Cut as it starts: it plays its first chunk's 10 ticks, then holds there, playing.
    cutOnStart = true;
    REQUIRE( net.ServerNet.SendKillcam( victim, killer, 3.0 ) );
    REQUIRE( runUntil( [&]() { return player.IsPlaying(); }, 2.0 ) );
    CHECK( player.GetStatus().Id == 1 );
    REQUIRE( runUntil( [&]() { return heldFrames >= 30; }, 2.0 ) );
    CHECK( player.IsPlaying() );
    CHECK( heldRight );
    CHECK_FALSE( beyondArrived );
    CHECK( heldAt == player.GetStatus().FirstTick + 9 );
    for( const std::string& problem : heldProblems )
    {
        FAIL_CHECK( problem );
    }
    CHECK( heldMatched >= 30 );     // the victim and the bot at least, each frame held
    CHECK_FALSE( net.ClientNet.GetKillcam().IsComplete() );
    // The link back: the chunks land, it goes on from where it held, to the death tick, and ends played.
    net.ServerLink.SetConditions( open );
    REQUIRE( runUntil( [&]() { return !player.IsPlaying(); }, 6.0 ) );
    REQUIRE( events.size() == 2 );
    CHECK( events[1].Reason == "played" );
    CHECK( shownAfterHeld >= static_cast<double>( player.GetStatus().DeathTick ) );
    CHECK( heldRight );
    CHECK_FALSE( beyondArrived );
    const uint64_t stalledFirst = player.GetStats().StalledFrames;
    MESSAGE( "held " << heldFrames << " frames on tick " << heldAt << " (" << heldMatched << " entity states checked there), then played on to " << shownAfterHeld );
    CHECK( stalledFirst >= 30 );

    // 2. Cut again, and it never comes back in time: it ends, failed, once it has waited MaxStallSeconds.
    player.GetSettings().MaxStallSeconds = 0.5;
    cutAt = -1.0;
    heldAt = 0;
    REQUIRE( net.ServerNet.SendKillcam( victim, killer, 3.0 ) );
    REQUIRE( runUntil( [&]() { return player.IsPlaying(); }, 2.0 ) );
    CHECK( player.GetStatus().Id == 2 );
    const double startedAt = net.Now();
    REQUIRE( runUntil( [&]() { return !player.IsPlaying(); }, 3.0 ) );
    REQUIRE( events.size() == 4 );
    CHECK( events[3].Id == 2 );
    CHECK( events[3].Reason.rfind( "failed: the stream stalled at tick ", 0 ) == 0 );
    CHECK( player.GetStats().Failed == 1 );
    // 10 ticks of stream, then half a second's wait (and a frame).
    CHECK( net.Now() - startedAt >= 10.0 / 60.0 + 0.5 - 0.02 );
    CHECK( net.Now() - startedAt < 10.0 / 60.0 + 0.5 + 0.1 );
    CHECK( player.GetStats().StalledFrames > stalledFirst );
    // Given up on, its download stops (ME-343): the server, told over the link back (still open), drops the rest;
    // what was on its way when the link comes back is cancelled too, and it isn't played again.
    CHECK( net.ClientNet.GetKillcam().IsCancelled() );
    REQUIRE( runUntil( [&]() { return net.ServerNet.GetKillcamsPending() == 0; }, 1.0 ) );
    CHECK( net.ServerNet.GetKillcamStats().CancelledByClient == 1 );
    net.ServerLink.SetConditions( open );
    net.Run( 1.0 );
    CHECK_FALSE( net.ClientNet.GetKillcam().IsComplete() );
    CHECK( net.ClientNet.GetClient()->GetConnection().GetBlobProgress().empty() );
    CHECK_FALSE( player.IsPlaying() );
    CHECK( player.GetStats().Started == 2 );
    CHECK( player.GetLastSeenId() == 2 );
    // The replay world built for the first, kept for the second (ME-343).
    CHECK( player.GetStats().WorldsBuilt == 1 );
    CHECK( player.GetStats().WorldsReused == 1 );

    // 3. A player kills (ME-343): its view of a tick is its client's tick whose command's view tick that is, its
    // interpolation delay further on in the stream. Cut as it starts, the killcam holds at the view tick of the
    // killer's last tick in, not on that tick (where the killer's view of it hasn't come); the link back, it goes on.
    chunking.ChunkTicks = 30;
    net.ServerNet.SetKillcamSettings( chunking );
    player.GetSettings().MaxStallSeconds = 3.0;
    Peer& peer = net.AddClient( Link( 50.f, 0.f, 0.f, 3245 ), [&]( World& InWorld ) {
        EntityHandle placed = InWorld.CreateEntityWithGUID( kVictimGuid );
        placed->AddComponent<Transform>();
        placed->AddComponent<NetIdentity>();
        InWorld.Simulate();
        InWorld.Start();
    } );
    net.MakeCommand = [&]( uint32_t InTick ) {
        InputCommand command;
        if( net.Making == &peer.Net )
        {
            command.HasAim = true;
            command.Yaw = static_cast<float>( ( InTick * 3 ) % 360 );
            command.Pitch = -4.f;
        }
        return command;
    };
    net.Run( 1.0 );
    REQUIRE( peer.Net.GetClient()->GetState() == NetClient::State::Connected );
    REQUIRE( peer.Net.GetClient()->GetClientId() == 2 );
    EntityHandle shooter = net.ServerWorld->CreateEntity();
    shooter->SetName( "Shooter" );
    shooter->AddComponent<Transform>().SetPosition( Vector3( -4.f, 0.f, 2.f ) );
    shooter->AddComponent<NetIdentity>().SetOwner( 2 );
    net.Run( 3.0 );
    cutAt = -1.0;
    stalledSeen = player.GetStats().StalledFrames;
    REQUIRE( net.ServerNet.SendKillcam( victim, shooter, 2.0 ) );
    REQUIRE( runUntil( [&]() { return player.IsPlaying(); }, 2.0 ) );
    CHECK( player.GetStatus().Id == 3 );
    CHECK( player.GetStatus().KillerClient == 2 );
    REQUIRE( runUntil( [&]() { return heldPlayerFrames >= 30; }, 2.0 ) );
    CHECK( player.IsPlaying() );
    CHECK( heldInView );
    CHECK( playerInStep );
    CHECK_FALSE( net.ClientNet.GetKillcam().IsComplete() );
    MESSAGE( "a player's killcam held " << heldPlayerFrames << " frames at tick " << player.GetStatus().Tick << ", the killer's last tick in " << player.GetStatus().KillerTick );
    net.ServerLink.SetConditions( open );
    REQUIRE( runUntil( [&]() { return !player.IsPlaying(); }, 6.0 ) );
    REQUIRE( events.size() == 6 );
    CHECK( events[5].Reason == "played" );
    CHECK( heldInView );
    CHECK( playerInStep );
    CHECK( playerFrames > 60 );

    // 4. The session ends: the ids it saw were its server's, so the next session's are all new.
    net.ClientNet.GetClient()->Disconnect( net.Now() );
    net.Run( 0.1 );
    CHECK_FALSE( net.ClientNet.IsSessionLive() );
    CHECK( player.GetLastSeenId() == 0 );
    CHECK( player.GetStats().Started == 3 );
    // Its kept replay world goes with the session (ME-343).
    CHECK( player.GetKeptWorld() == nullptr );
    CHECK( player.GetStats().WorldsBuilt == 1 );
    CHECK( player.GetStats().WorldsReused == 2 );
    net.OnMillisecond = nullptr;
}


TEST_CASE( "Killcam: the killer's view: the net aim as the camera's front, a point's place in the frame, and the killer's tick that looked at a time (ME-343)" )
{
    // The net aim convention: yaw about +Y, 0 down +Z, 90 down +X; pitch up from level.
    const auto near = []( const Vector3& InA, const Vector3& InB ) { return ( InA - InB ).Length() < 1e-4f; };
    CHECK( near( KillcamPlayer::AimDirection( 0.f, 0.f ), Vector3( 0.f, 0.f, 1.f ) ) );
    CHECK( near( KillcamPlayer::AimDirection( 90.f, 0.f ), Vector3( 1.f, 0.f, 0.f ) ) );
    CHECK( near( KillcamPlayer::AimDirection( 180.f, 0.f ), Vector3( 0.f, 0.f, -1.f ) ) );
    CHECK( near( KillcamPlayer::AimDirection( 0.f, 90.f ), Vector3( 0.f, 1.f, 0.f ) ) );
    CHECK( near( KillcamPlayer::AimDirection( 270.f, -45.f ), Vector3( -0.70710678f, -0.70710678f, 0.f ) ) );

    // A camera put at an eye looking along an aim (as PlaceView does) has that front, the level quarter turn
    // as its right and Project's up as its up: Project's frame is the camera's.
    for( const auto& aim : { std::pair<float, float>{ 0.f, 0.f }, { 122.7f, -18.5f }, { 311.f, 40.f }, { 212.3f, -39.6f } } )
    {
        INFO( "yaw " << aim.first << " pitch " << aim.second );
        auto world = std::make_shared<World>();
        world->IsLoading = false;
        EntityHandle camera = world->CreateEntity( "Camera" );
        Transform& transform = camera->AddComponent<Transform>();
        const Vector3 eye( -9.05f, 3.05f, -2.5f );
        transform.SetWorldPosition( eye );
        transform.LookAt( eye + KillcamPlayer::AimDirection( aim.first, aim.second ) );
        const Vector3 front = transform.Front().Normalized();
        CHECK( near( front, KillcamPlayer::AimDirection( aim.first, aim.second ) ) );
        // A point ahead, up and to the right of the view lands up and right in the frame, as far ahead as it is.
        const Vector3 right = KillcamPlayer::AimDirection( aim.first + 90.f, 0.f );
        const Vector3 up = front.Cross( right );
        CHECK( std::abs( up.Dot( front ) ) < 1e-4f );
        CHECK( up.Dot( Vector3( 0.f, 1.f, 0.f ) ) > 0.f );
        const KillcamPlayer::ViewPoint point = KillcamPlayer::Project( eye, aim.first, aim.second, 90.f, 2.f, eye + front * 10.f + right * 5.f + up * 2.5f );
        CHECK( point.Inside );
        CHECK( std::abs( point.Depth - 10.f ) < 1e-3f );
        CHECK( std::abs( point.X - 0.25f ) < 1e-4f );   // 5 m right at 10 m: half of the half width (10 x tan 45 x 2)
        CHECK( std::abs( point.Y - 0.25f ) < 1e-4f );   // 2.5 m up: a quarter of the half height
    }
    {
        // Centre, edges and behind; a 75-degree view (HeroShooter's) at 16:9.
        const Vector3 eye( 1.f, 2.f, 3.f );
        const float half = std::tan( 37.5f * 3.14159265f / 180.f );
        KillcamPlayer::ViewPoint point = KillcamPlayer::Project( eye, 90.f, 0.f, 75.f, 16.f / 9.f, eye + Vector3( 6.f, 0.f, 0.f ) );
        CHECK( point.Inside );
        CHECK( std::abs( point.X ) < 1e-5f );
        CHECK( std::abs( point.Y ) < 1e-5f );
        point = KillcamPlayer::Project( eye, 90.f, 0.f, 75.f, 16.f / 9.f, eye + Vector3( 6.f, 6.f * half * 0.99f, 0.f ) );
        CHECK( point.Inside );
        CHECK( point.Y > 0.98f );
        point = KillcamPlayer::Project( eye, 90.f, 0.f, 75.f, 16.f / 9.f, eye + Vector3( 6.f, 0.f, -6.f * half * 16.f / 9.f * 1.01f ) );
        CHECK_FALSE( point.Inside );    // off the right edge (looking down +X, -Z is right)
        CHECK( point.X > 1.f );
        point = KillcamPlayer::Project( eye, 90.f, 0.f, 75.f, 16.f / 9.f, eye + Vector3( -0.75f, -0.75f, 0.f ) );
        CHECK_FALSE( point.Inside );    // behind the eye
        CHECK( point.Depth < 0.f );
    }

    // The killer's ticks 100..130: its client 12.5 ticks behind (its view tick), walking down +X 0.1 m a
    // tick, turning 3 degrees a tick through north (345 at tick 100: 0 at 105).
    std::vector<KillcamPlayer::KillerSample> samples;
    for( uint32_t tick = 100; tick <= 130; ++tick )
    {
        KillcamPlayer::KillerSample sample;
        sample.Tick = tick;
        sample.HasAim = true;
        sample.Yaw = std::fmod( 345.f + ( tick - 100 ) * 3.f, 360.f );
        sample.Pitch = -10.f + ( tick - 100 ) * 0.5f;
        sample.ViewTick = tick - 12.5;
        sample.HasPosition = true;
        sample.Position = Vector3( tick * 0.1f, 1.f, -2.f );
        samples.push_back( sample );
    }
    // The world at tick 92 was on its screen between ticks 104 (view 91.5) and 105 (92.5).
    KillcamPlayer::KillerView view = KillcamPlayer::FindKillerView( samples, 92.0 );
    CHECK( std::abs( view.Tick - 104.5 ) < 1e-9 );
    CHECK( std::abs( view.ViewTick - 92.0 ) < 1e-9 );     // the tick it was looking at: the one asked about
    REQUIRE( view.HasAim );
    CHECK( std::abs( view.Yaw - 358.5f ) < 1e-3f );      // 357 to 0 the short way round
    CHECK( std::abs( view.Pitch - ( -10.f + 4.5f * 0.5f ) ) < 1e-4f );
    REQUIRE( view.HasPosition );
    CHECK( near( view.Position, Vector3( 10.45f, 1.f, -2.f ) ) );
    // On a view tick exactly: that tick. Before the first sample's view: the first; past the last's: the
    // last (the killer's last command: the kill).
    CHECK( std::abs( KillcamPlayer::FindKillerView( samples, 107.5 ).Tick - 120.0 ) < 1e-9 );
    CHECK( KillcamPlayer::FindKillerView( samples, 50.0 ).Tick == 100.0 );
    view = KillcamPlayer::FindKillerView( samples, 125.0 );
    CHECK( view.Tick == 130.0 );
    CHECK( view.ViewTick == 117.5 );    // held: looking at an earlier tick than the one asked about
    CHECK( std::abs( view.Yaw - std::fmod( 345.f + 90.f, 360.f ) ) < 1e-3f );
    // An old recording's commands have no view tick: the tick itself.
    for( KillcamPlayer::KillerSample& sample : samples )
    {
        sample.ViewTick = -1.0;
    }
    CHECK( std::abs( KillcamPlayer::FindKillerView( samples, 110.25 ).Tick - 110.25 ) < 1e-9 );
    // A tick without the killer's aim or position (a gap in the state) takes them from its neighbour.
    samples[11].HasAim = false;
    samples[11].HasPosition = false;
    view = KillcamPlayer::FindKillerView( samples, 110.25 );
    REQUIRE( view.HasAim );
    CHECK( std::abs( view.Yaw - samples[10].Yaw ) < 1e-4f );
    CHECK( near( view.Position, samples[10].Position ) );
    CHECK_FALSE( KillcamPlayer::FindKillerView( {}, 3.0 ).HasAim );

    // A recorded tick as a sample: its client's command and the killer's state.
    NetReplayTick tick;
    tick.Tick = 812;
    NetReplayClient other;
    other.Id = 1;
    other.Command.HasAim = true;
    other.Command.Yaw = 10.f;
    NetReplayClient killer;
    killer.Id = 2;
    killer.Command.HasAim = true;
    killer.Command.Yaw = 5.72f;
    killer.Command.Pitch = -4.61f;
    killer.Command.ViewTick = 797.75;
    tick.Clients = { other, killer };
    NetEntityState state;
    state.SetPose( Vector3( -5.5f, 0.895f, 3.25f ), Quaternion() );
    tick.State.Entities[65539] = state;
    KillcamPlayer::KillerSample sample = KillcamPlayer::MakeKillerSample( tick, 2, 65539 );
    CHECK( sample.Tick == 812 );
    CHECK( sample.HasAim );
    CHECK( sample.Yaw == 5.72f );
    CHECK( sample.Pitch == -4.61f );
    CHECK( sample.ViewTick == 797.75 );
    REQUIRE( sample.HasPosition );
    CHECK( ( sample.Position - Vector3( -5.5f, 0.895f, 3.25f ) ).Length() < 2e-3f );
    sample = KillcamPlayer::MakeKillerSample( tick, 3, 65540 );
    CHECK_FALSE( sample.HasAim );
    CHECK_FALSE( sample.HasPosition );
    CHECK( sample.ViewTick < 0.0 );
}
