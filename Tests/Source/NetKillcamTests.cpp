#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetHitbox.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
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
    // its pose) and an AlwaysRelevant arrow at 300 m that's destroyed a second before the kills.
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
    EntityHandle killer = net.ServerWorld->CreateEntity();
    killer->SetName( "Bot" );
    killer->AddComponent<Transform>();
    killer->AddComponent<NetIdentity>();
    arrow = net.ServerWorld->CreateEntity();
    arrow->AddComponent<Transform>().SetPosition( Vector3( 300.f, 2.f, 0.f ) );
    arrow->AddComponent<NetIdentity>().AlwaysRelevant = true;
    net.Run( 1.0 );
    const uint32_t arrowId = arrow->GetComponent<NetIdentity>().GetNetId();
    REQUIRE( arrowId != 0 );
    net.ServerWorld->DestroyEntity( arrow );
    arrow = EntityHandle();
    net.Run( 1.0 );
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
            ++arrowTicks;
        }
        ++compared;
    }
    MESSAGE( compared << " ticks of the bot's killcam; the arrow in " << arrowTicks << ", the post held through " << postMoved << " moves" );
    CHECK( compared == 180 );
    CHECK( arrowTicks > 30 );
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
