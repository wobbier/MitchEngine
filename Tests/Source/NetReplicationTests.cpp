#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/Rigidbody.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Net/BitStream.h"
#include "Net/NetSnapshot.h"
#include "NetHarness.h"
#include <cmath>
#include <map>
#include <set>

namespace NetReplicationTest
{
    // A component with replicated fields of each kind NetSchema handles.
    class Scoreboard
        : public Component<Scoreboard>
    {
        ME_REFLECTABLE( Scoreboard )
    public:
        Scoreboard() : Component( "NetReplicationTest::Scoreboard" ) {}
        int Score = 0;
        float Heat = 0.f;
        Vector3 Tint;
        std::string Label;
        bool Alive = true;
        float LocalOnly = 0.f;
    };

    struct TestContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

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

ME_REFLECT_BEGIN( NetReplicationTest::Scoreboard )
    ME_FIELD( Score ).Replicated();
    ME_FIELD( Heat ).Replicated();
    ME_FIELD( Tint ).Replicated();
    ME_FIELD( Label ).Replicated();
    ME_FIELD( Alive ).Replicated();
    ME_FIELD( LocalOnly );
ME_REFLECT_END()
ME_REGISTER_COMPONENT( NetReplicationTest::Scoreboard )

using namespace NetReplicationTest;
using namespace NetHarness;

TEST_CASE( "Net: NetSchema writes the Replicated fields from reflection, and only those" )
{
    auto world = std::make_shared<World>();
    EntityHandle a = world->CreateEntity();
    Scoreboard& source = a->AddComponent<Scoreboard>();
    source.Score = -42;
    source.Heat = 0.75f;
    source.Tint = Vector3( 1.f, 0.5f, 0.25f );
    source.Label = "red team";
    source.Alive = false;
    source.LocalOnly = 9.f;
    CHECK( NetSchema::CountReplicatedFields( *a.Get() ) == 5 );

    EntityHandle b = world->CreateEntity();
    Scoreboard& target = b->AddComponent<Scoreboard>();
    NetSchema::Read( *b.Get(), NetSchema::Write( *a.Get() ) );
    CHECK( target.Score == -42 );
    CHECK( target.Heat == 0.75f );
    CHECK( target.Tint.y == 0.5f );
    CHECK( target.Label == "red team" );
    CHECK_FALSE( target.Alive );
    CHECK( target.LocalOnly == 0.f );
}

TEST_CASE( "Net: snapshot deltas carry only what changed, list what's gone, and respect the budget" )
{
    NetSnapshot first;
    first.Tick = 10;
    for( uint32_t id = 1; id <= 20; ++id )
    {
        first.Entities[id].SetPose( Vector3( static_cast<float>( id ), 1.f, -2.5f ), Quaternion( 0.f, 0.f, 0.f, 1.f ) );
    }
    BitWriter full;
    NetSnapshot sentFirst;
    NetSnapshotCodec::Write( full, first, nullptr, 900, sentFirst );
    NetSnapshot decodedFirst;
    BitReader fullReader( full.GetBytes() );
    REQUIRE( NetSnapshotCodec::Read( fullReader, nullptr, decodedFirst ) );
    CHECK( decodedFirst.Entities == first.Entities );
    // Positions come back within 1/1024 m.
    CHECK( std::abs( decodedFirst.Entities[7].GetPosition().x - 7.f ) <= 1.f / 1024.f );

    NetSnapshot second = first;
    second.Tick = 11;
    second.Entities[3].SetPose( Vector3( 3.f, 0.5f, -2.5f ), Quaternion::FromEulerDegrees( Vector3( 0.f, 45.f, 0.f ) ) );
    second.Entities.erase( 5 );
    second.Entities[30].SetPose( Vector3( 0.f, 10.f, 0.f ), Quaternion( 0.f, 0.f, 0.f, 1.f ) );
    BitWriter delta;
    NetSnapshot sentSecond;
    NetSnapshotCodec::Write( delta, second, &decodedFirst, 900, sentSecond );
    CHECK( delta.GetByteCount() < full.GetByteCount() / 4 );
    NetSnapshot decodedSecond;
    BitReader deltaReader( delta.GetBytes() );
    REQUIRE( NetSnapshotCodec::Read( deltaReader, &decodedFirst, decodedSecond ) );
    CHECK( decodedSecond.Entities == second.Entities );
    // A delta against a baseline the reader doesn't have is refused.
    BitReader wrongReader( delta.GetBytes() );
    NetSnapshot wrong;
    CHECK_FALSE( NetSnapshotCodec::Read( wrongReader, &second, wrong ) );

    // Nothing changed: a handful of bytes.
    BitWriter idle;
    NetSnapshot sentIdle;
    NetSnapshot third = second;
    third.Tick = 12;
    NetSnapshotCodec::Write( idle, third, &decodedSecond, 900, sentIdle );
    CHECK( idle.GetByteCount() <= 10 );

    // Over budget: some wait, and "sent" says what the reader will really have.
    BitWriter tight;
    NetSnapshot sentTight;
    NetSnapshotCodec::Write( tight, first, nullptr, 60, sentTight );
    CHECK( sentTight.Entities.size() < first.Entities.size() );
    NetSnapshot decodedTight;
    BitReader tightReader( tight.GetBytes() );
    REQUIRE( NetSnapshotCodec::Read( tightReader, nullptr, decodedTight ) );
    CHECK( decodedTight.Entities == sentTight.Entities );
}

TEST_CASE( "Net: 50 falling boxes mirror within 1 cm at the render tick under loss; idle costs < 1 kbps; despawns and a late joiner converge" )
{
    PhysicsCore physics;    // outlives the harness's server world
    Harness net( Link( 50.f, 10.f, 0.05f, 31 ), Link( 50.f, 10.f, 0.05f, 32 ) );
    net.ServerWorld->AddCore( physics );
    EntityHandle ground = net.ServerWorld->CreateEntity( "Ground" );
    ground->AddComponent<Transform>().SetPosition( Vector3( 0.f, -0.5f, 0.f ) );
    ground->AddComponent<BoxCollider>().Size = Vector3( 60.f, 1.f, 60.f );

    // Half placed (static NetIds, in both worlds, the client's with a body to make kinematic),
    // half spawned once the session runs (dynamic NetIds, reliable spawns).
    auto makeBox = []( EntityHandle InBox, int InIndex ) {
        InBox->GetComponent<Transform>().SetPosition( Vector3( ( InIndex % 10 ) * 2.5f - 11.f, 3.f + ( InIndex / 10 ) * 1.5f, ( InIndex % 3 ) * 2.f ) );
        InBox->GetComponent<Transform>().SetRotation( Vector3( InIndex * 7.f, InIndex * 13.f, 0.f ) );
        InBox->AddComponent<Rigidbody>().Mass = 1.f;
        InBox->AddComponent<BoxCollider>().Size = Vector3( 1.f, 1.f, 1.f );
    };
    for( int i = 0; i < 25; ++i )
    {
        net.Place( 0xB0C5000 + i );
    }
    std::vector<EntityHandle> clientPlaced;
    int placedIndex = 0;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() >= 0xB0C5000 && InEntity.GetGUID() < 0xB0C5000 + 25 )
        {
            EntityHandle box = net.ServerWorld->GetEntity( InEntity.GetId() );
            makeBox( box, static_cast<int>( InEntity.GetGUID() - 0xB0C5000 ) );
            ++placedIndex;
        }
    } );
    net.ClientWorld->ForEachEntity( [&]( Entity& InEntity ) {
        EntityHandle box = net.ClientWorld->GetEntity( InEntity.GetId() );
        box->AddComponent<Rigidbody>();
        clientPlaced.push_back( box );
    } );
    REQUIRE( placedIndex == 25 );

    net.ServerWorld->Simulate();
    net.ServerWorld->Start();
    net.Start();
    for( int i = 25; i < 50; ++i )
    {
        EntityHandle box = net.ServerWorld->CreateEntity();
        box->AddComponent<Transform>();
        box->AddComponent<NetIdentity>();
        makeBox( box, i );
    }

    // The server simulates; its history is what the clients should show.
    std::map<uint32_t, std::map<uint32_t, Vector3>> history;
    net.OnServerTick = [&]( NetServer& InServer ) {
        TestContext context;
        context.FixedDeltaTime = 1.f / 60.f;
        context.InterpolationAlpha = 1.f;
        net.ServerWorld->Simulate();
        context.IsFixedStepActive = true;
        physics.FixedUpdate( context );
        context.IsFixedStepActive = false;
        physics.Update( context );
        std::map<uint32_t, Vector3>& tick = history[InServer.GetTick()];
        net.ServerWorld->ForEachEntity( [&tick]( Entity& InEntity ) {
            if( const NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>(); identity && identity->GetNetId() != 0 )
            {
                tick[identity->GetNetId()] = InEntity.GetComponent<Transform>().GetWorldPosition();
            }
        } );
    };
    // ... compared with what the client shows, every frame, at its render tick.
    size_t samples = 0;
    size_t within = 0;
    float worst = 0.f;
    bool measuring = false;
    net.OnClientFrame = [&]( NetClient& ) {
        NetReplicationClient* replication = net.ClientNet.GetReplicationClient();
        const double render = replication->GetRenderTick();
        const uint32_t below = static_cast<uint32_t>( std::floor( render ) );
        auto a = history.find( below );
        auto b = history.find( below + 1 );
        if( !measuring || a == history.end() || b == history.end() )
        {
            return;
        }
        const float t = static_cast<float>( render - below );
        for( const auto& [id, position] : a->second )
        {
            EntityHandle mirrored = replication->FindEntity( id );
            auto next = b->second.find( id );
            if( !mirrored || next == b->second.end() )
            {
                continue;
            }
            const Vector3 expected = position + ( next->second - position ) * t;
            const float error = ( mirrored->GetComponent<Transform>().GetWorldPosition() - expected ).Length();
            worst = std::max( worst, error );
            within += error <= 0.01f ? 1 : 0;
            ++samples;
        }
    };

    net.Run( 0.5 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    measuring = true;
    net.Run( 4.0 );     // they fall, land and settle
    NetReplicationClient& client = *net.ClientNet.GetReplicationClient();
    CHECK( client.GetEntityCount() == 50 );
    MESSAGE( "mirror: " << within << "/" << samples << " within 1 cm, worst " << worst * 100.f << " cm, delay " << client.GetDelayTicks() << " ticks" );
    CHECK( samples > 50 * 200 );
    CHECK( static_cast<double>( within ) / samples >= 0.995 );
    // The rest: a box hitting the ground inside a lost-snapshot gap, which straight-line
    // interpolation across the gap can't know about.
    CHECK( worst < 0.1f );
    // Remote bodies are kinematic: only the server simulates.
    for( EntityHandle& box : clientPlaced )
    {
        CHECK( box->GetComponent<Rigidbody>().Type == BodyType::Kinematic );
    }

    // Idle: everything asleep, the snapshots cost next to nothing.
    measuring = false;
    net.Run( 4.0 );
    NetReplicationServer& server = *net.ServerNet.GetReplicationServer();
    const uint64_t bytesBefore = server.GetSnapshotBytes();
    net.Run( 10.0 );
    const double idleBitsPerSecond = static_cast<double>( server.GetSnapshotBytes() - bytesBefore ) * 8.0 / 10.0;
    MESSAGE( "idle snapshots: " << idleBitsPerSecond << " bit/s" );
    CHECK( idleBitsPerSecond < 1000.0 );

    // Despawns, and a client that joins now, converge on the server's world.
    int destroyed = 0;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( const NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>(); identity && identity->GetNetId() % 7 == 0 )
        {
            net.ServerWorld->DestroyEntity( net.ServerWorld->GetEntity( InEntity.GetId() ) );
            ++destroyed;
        }
    } );
    REQUIRE( destroyed > 0 );
    Peer& late = net.AddClient( Link( 70.f, 10.f, 0.05f, 33 ), [&]( World& InWorld ) {
        for( int i = 0; i < 25; ++i )
        {
            EntityHandle box = InWorld.CreateEntityWithGUID( 0xB0C5000 + i );
            box->AddComponent<Transform>();
            box->AddComponent<NetIdentity>();
        }
    } );
    net.Run( 3.0 );
    std::set<uint32_t> serverIds;
    for( const auto& [id, state] : server.GetCurrent().Entities )
    {
        serverIds.insert( id );
    }
    CHECK( serverIds.size() == static_cast<size_t>( 50 - destroyed ) );
    for( NetReplicationClient* mirror : { net.ClientNet.GetReplicationClient(), late.Net.GetReplicationClient() } )
    {
        REQUIRE( mirror != nullptr );
        std::set<uint32_t> mirroredIds;
        for( const uint32_t id : serverIds )
        {
            EntityHandle entity = mirror->FindEntity( id );
            if( entity )
            {
                mirroredIds.insert( id );
                const Vector3 there = server.GetCurrent().Entities.at( id ).GetPosition();
                CHECK( ( entity->GetComponent<Transform>().GetWorldPosition() - there ).Length() < 0.002f );
            }
        }
        CHECK( mirroredIds == serverIds );
        // Destroyed placed boxes are gone from the mirrors too (the late joiner placed them all).
        CHECK( mirror->GetEntityCount() == serverIds.size() );
    }
}


TEST_CASE( "Net: a 500 KB blob at 150 ms RTT and 5% loss leaves the snapshot rate within 5%, commands and the clock alone, and the link within budget" )
{
    // ME-323 (the killcam's channel): live traffic keeps its budget while a big blob streams.
    Harness net( Link( 75.f, 5.f, 0.05f, 41 ), Link( 75.f, 5.f, 0.05f, 42 ) );
    net.Start();
    // 20 movers, so every tick has a snapshot to send.
    std::vector<EntityHandle> movers;
    for( int i = 0; i < 20; ++i )
    {
        EntityHandle mover = net.ServerWorld->CreateEntity();
        mover->AddComponent<Transform>();
        mover->AddComponent<NetIdentity>();
        movers.push_back( mover );
    }
    net.OnServerTick = [&]( NetServer& InServer ) {
        const float t = static_cast<float>( InServer.GetTick() ) / 60.f;
        for( size_t i = 0; i < movers.size(); ++i )
        {
            const float phase = t + static_cast<float>( i );
            movers[i]->GetComponent<Transform>().SetPosition( Vector3( std::sin( phase ) * 5.f, 0.f, std::cos( phase ) * 5.f + static_cast<float>( i ) ) );
        }
    };
    net.Run( 3.0 );
    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    NetServer::Client& onServer = *net.ServerNet.GetServer()->GetClients().front();
    REQUIRE( onServer.Accepted );
    std::vector<uint8_t> received;
    double receivedAt = -1.0;
    client.OnBlob = [&]( uint32_t, std::vector<uint8_t>& InData ) {
        received = std::move( InData );
        receivedAt = net.Now();
    };

    struct Counts
    {
        uint64_t Snapshots = 0;
        uint64_t Misses = 0;
        int Resyncs = 0;
        uint64_t BytesDown = 0;
        double At = 0.0;
    };
    auto count = [&]() {
        Counts counts;
        counts.Snapshots = net.ClientNet.GetReplicationClient()->GetSnapshotsReceived();
        counts.Misses = onServer.Commands.GetMissedCount();
        counts.Resyncs = client.GetClock().GetResyncCount();
        counts.BytesDown = onServer.Connection->GetStats().BytesSent;
        counts.At = net.Now();
        return counts;
    };

    // Without a blob.
    const Counts before = count();
    net.Run( 5.0 );
    const Counts baseline = count();
    const double baselineRate = static_cast<double>( baseline.Snapshots - before.Snapshots ) / ( baseline.At - before.At );

    // With one: from the send until the client has it whole.
    std::vector<uint8_t> blob( 500 * 1024 );
    for( size_t i = 0; i < blob.size(); ++i )
    {
        blob[i] = static_cast<uint8_t>( ( i * 131 ) ^ ( i >> 11 ) );
    }
    REQUIRE( net.ServerNet.GetServer()->SendBlob( onServer, blob ) != 0 );
    for( int i = 0; i < 300 && receivedAt < 0.0; ++i )
    {
        net.Run( 0.1 );
    }
    REQUIRE( receivedAt > 0.0 );
    const Counts during = count();
    CHECK( received == blob );
    const double seconds = during.At - baseline.At;
    const double duringRate = static_cast<double>( during.Snapshots - baseline.Snapshots ) / seconds;
    const double linkRate = static_cast<double>( during.BytesDown - baseline.BytesDown ) / seconds;
    const NetStats& stats = onServer.Connection->GetStats();
    MESSAGE( "snapshots " << baselineRate << "/s without, " << duringRate << "/s during the " << seconds << " s transfer; link " << linkRate << " B/s; "
                          << stats.BlobSlicesResent << " slices resent; misses " << baseline.Misses - before.Misses << " then " << during.Misses - baseline.Misses );
    CHECK( baselineRate > 50.0 );
    CHECK( std::abs( duringRate - baselineRate ) <= baselineRate * 0.05 );
    // Commands: no more misses than without (per second), and the clock never jumped.
    CHECK( static_cast<double>( during.Misses - baseline.Misses ) / seconds <= static_cast<double>( baseline.Misses - before.Misses ) / 5.0 + 0.2 );
    CHECK( during.Resyncs == baseline.Resyncs );
    // Within the link's budget, and the blob really did use it (not a trickle).
    const double budget = onServer.Connection->GetBlobSettings().LinkBytesPerSecond;
    CHECK( linkRate <= budget * 1.03 );
    CHECK( seconds < 8.0 );
    CHECK( stats.BlobSlicesResent > 0 );
}
