#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetHitbox.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Net/BitStream.h"
#include "Net/NetCommands.h"
#include "Net/NetDebug.h"
#include "NetHarness.h"
#include <cmath>
#include <cstring>
#include <map>

// NET-7 runtime debugging: the shooter's hit-result message, the per-entity snapshot accounting.
namespace NetDebugTest
{
    struct TestContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

    NetConditions Link( float InLatencyMs, float InJitterMs, uint64_t InSeed )
    {
        NetConditions conditions;
        conditions.LatencyMs = InLatencyMs;
        conditions.JitterMs = InJitterMs;
        conditions.Seed = InSeed;
        return conditions;
    }

    // Strafes along X between -4 and 4 at 5 m/s, 20 m out.
    Vector3 TargetAt( uint32_t InTick )
    {
        const float distance = 5.f * InTick / 60.f;
        const float phase = std::fmod( distance, 16.f );
        const float x = phase < 8.f ? phase - 4.f : 12.f - phase;
        return Vector3( x, 1.f, 20.f );
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

    Vector3 Aim( const InputCommand& InCommand )
    {
        const float yaw = InCommand.Yaw * 3.14159265f / 180.f;
        const float pitch = InCommand.Pitch * 3.14159265f / 180.f;
        return Vector3( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
    }

    bool SameBits( float InA, float InB )
    {
        return std::memcmp( &InA, &InB, sizeof( float ) ) == 0;
    }

    bool SameBits( const Vector3& InA, const Vector3& InB )
    {
        return SameBits( InA.x, InB.x ) && SameBits( InA.y, InB.y ) && SameBits( InA.z, InB.z );
    }

    bool SamePose( const NetReplayHitbox& InHitbox, const NetLagCompensation::HitboxPose& InPose )
    {
        return SameBits( InHitbox.Position, InPose.Position ) && SameBits( InHitbox.Rotation.x, InPose.Rotation.x ) && SameBits( InHitbox.Rotation.y, InPose.Rotation.y )
            && SameBits( InHitbox.Rotation.z, InPose.Rotation.z ) && SameBits( InHitbox.Rotation.w, InPose.Rotation.w ) && SameBits( InHitbox.Radius, InPose.Radius )
            && SameBits( InHitbox.HalfHeight, InPose.HalfHeight ) && SameBits( InHitbox.HalfExtents, InPose.HalfExtents ) && InHitbox.Occluder == InPose.Occluder
            && InHitbox.Shape == InPose.Shape;
    }

    // Where the server had the target at a (fractional) tick: the recorded ticks' positions, lerped.
    Vector3 TargetAtTick( double InTick )
    {
        const double whole = std::floor( InTick );
        const float t = static_cast<float>( InTick - whole );
        const Vector3 a = TargetAt( static_cast<uint32_t>( whole ) );
        const Vector3 b = TargetAt( static_cast<uint32_t>( whole ) + 1 );
        return a + ( b - a ) * t;
    }

    // From InPoint to the axis of a vertical capsule about InCentre.
    float FromAxis( const Vector3& InPoint, const Vector3& InCentre, float InHalfHeight )
    {
        const float y = std::clamp( InPoint.y - InCentre.y, -InHalfHeight, InHalfHeight );
        return ( InPoint - ( InCentre + Vector3( 0.f, y, 0.f ) ) ).Length();
    }

    EntityHandle Find( World& InWorld, uint64_t InGUID )
    {
        EntityHandle found;
        InWorld.ForEachEntity( [&]( Entity& InEntity ) {
            if( InEntity.GetGUID() == InGUID )
            {
                found = InWorld.GetEntity( InEntity.GetId() );
            }
        } );
        return found;
    }
}

using namespace NetDebugTest;
using namespace NetHarness;

TEST_CASE( "Net: at 150 ms every shot's debug result is the server's rewound raycast, and the client pairs it with its own prediction (NET-7)" )
{
    PhysicsCore serverPhysics;
    PhysicsCore clientPhysics;
    Harness net( Link( 75.f, 5.f, 81 ), Link( 75.f, 5.f, 82 ) );
    net.ServerWorld->AddCore( serverPhysics );
    net.ClientWorld->AddCore( clientPhysics );
    net.Place( 0x7A26EA );     // the target, mirrored to the client
    net.Place( 0x5402 );       // the shooter: the client's predicted pawn
    EntityHandle target = Find( *net.ServerWorld, 0x7A26EA );
    EntityHandle clientTarget = Find( *net.ClientWorld, 0x7A26EA );
    EntityHandle shooter = Find( *net.ServerWorld, 0x5402 );
    EntityHandle clientShooter = Find( *net.ClientWorld, 0x5402 );
    // Scenes put hitboxes on both sides: the client tests its own view of them.
    for( EntityHandle body : { target, clientTarget } )
    {
        NetHitbox& hitbox = body->AddComponent<NetHitbox>();
        hitbox.Radius = 0.4f;
        hitbox.Height = 1.8f;
    }
    const Vector3 muzzle( 0.f, 1.f, -10.f );
    for( EntityHandle pawn : { shooter, clientShooter } )
    {
        pawn->GetComponent<Transform>().SetPosition( muzzle );
        pawn->AddComponent<CharacterController>().GravityScale = 0.f;
    }
    shooter->GetComponent<NetIdentity>().SetOwner( 1 );
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        world->Simulate();
        world->Start();
    }

    // The client aims at the target where it shows it and fires every 20 ticks (until the tail).
    bool firing = true;
    net.MakeCommand = [&]( uint32_t InTick ) {
        InputCommand command;
        const Vector3 to = clientTarget->GetComponent<Transform>().GetWorldPosition() + Vector3( 0.f, 0.3f, 0.f ) - muzzle;
        command.HasAim = true;
        command.Yaw = std::atan2( to.x, to.z ) * 180.f / 3.14159265f;
        command.Pitch = std::atan2( to.y, std::sqrt( to.x * to.x + to.z * to.z ) ) * 180.f / 3.14159265f;
        command.Buttons = firing && InTick % 20 == 0 ? 2u : 0u;
        return command;
    };
    // Both sides fire the same ray from the same (quantized) command, as a game's [Predicted] script would.
    struct Judged
    {
        LagCompensatedHit Hit;
        double ViewTick = 0.0;
        std::vector<NetLagCompensation::HitboxPose> Rewound;
        std::vector<NetLagCompensation::HitboxPose> Live;
    };
    std::map<uint32_t, Judged> judged;     // by command tick
    net.ServerNet.SetCommandHandler( [&]( Entity& InShooter, const InputCommand& InCommand ) {
        if( !( InCommand.Buttons & 2u ) || InCommand.ViewTick < 0.0 || !InCommand.HasAim )
        {
            return;
        }
        Judged& record = judged[InCommand.Tick];
        record.Hit = net.ServerNet.LagCompensatedRaycast( muzzle, Aim( InCommand ), 100.f, InCommand, net.ServerWorld->GetEntity( InShooter.GetId() ) );
        record.ViewTick = InCommand.ViewTick;
        const NetLagCompensation& lag = net.ServerNet.GetLagCompensation();
        record.Rewound = lag.GetPosesAt( static_cast<double>( lag.GetNewestTick() ) - record.Hit.RewoundTicks );
        record.Live = lag.GetPosesAt( static_cast<double>( lag.GetNewestTick() ) );
    } );
    net.ClientNet.SetCommandHandler( []( Entity&, const InputCommand& ) {} );
    int clientShots = 0;
    net.OnClientTick = [&]( NetCore& InNet ) {
        const InputCommand& command = InNet.GetPredictor()->GetCommand();
        if( ( command.Buttons & 2u ) && command.HasAim )
        {
            InNet.PresentRaycast( muzzle, Aim( command ), 100.f, clientShooter );
            ++clientShots;
        }
        Step( clientPhysics, *net.ClientWorld );
    };
    net.OnServerTick = [&]( NetServer& InServer ) {
        target->GetComponent<Transform>().SetPosition( TargetAt( InServer.GetTick() ) );
        Step( serverPhysics, *net.ServerWorld );
    };
    // The client's Net Debugger asks for shots (and bandwidth); the server counts nothing of its own.
    NetDebugSubscription debug;
    debug.Shots = true;
    debug.Bandwidth = true;
    net.ClientNet.SetDebug( debug );
    net.Start();
    net.ClientNet.Predict( clientShooter );
    REQUIRE( net.ClientNet.GetPredictor() );
    net.Run( 22.0 );
    // A quiet tail: the last bundles land.
    firing = false;
    net.Run( 1.5 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );

    const NetShotLedger& ledger = net.ClientNet.GetShotLedger();
    const NetShotLedger::Totals& totals = ledger.GetTotals();
    MESSAGE( clientShots << " shots fired by the client, " << judged.size() << " judged by the server, " << totals.Results << " results back; " << totals.Agreed << " of "
        << totals.Paired << " agree, " << totals.ClientOnly << " client only, " << totals.ServerOnly << " server only, " << ledger.GetPendingCount()
        << " waiting; hit points " << ( totals.PointPairs ? totals.SumPointDelta / totals.PointPairs * 1000.0 : 0.0 ) << " mm apart on average" );
    REQUIRE( judged.size() >= 50 );
    // Every judged shot came back, in bundles (blobs), each paired with its prediction.
    const NetCore::DebugStats& stats = net.ServerNet.GetDebugStats();
    CHECK( totals.Results == judged.size() );
    CHECK( ledger.GetPendingCount() == 0 );
    CHECK( totals.ServerOnly == 0 );
    CHECK( totals.ClientOnly == 0 );
    CHECK( totals.Paired == totals.Results );
    CHECK( totals.Agreed * 100 >= totals.Paired * 95 );
    CHECK( stats.ShotResults == judged.size() );
    CHECK( stats.ShotResultsDropped == 0 );
    CHECK( stats.ShotBundles >= 1 );
    CHECK( net.ClientNet.GetShotBundlesReceived() == stats.ShotBundles );

    // Each result is the server's own raycast, to the bit: the hit, its point, the view tick, the
    // rewind, the target, and the target's hitbox as it was rewound and as it was live. And, from the
    // target's known path rather than the lag compensator's own lookup, the rewound hitbox is where
    // the server had the target at the client's view tick, and a hit point lies on that capsule.
    const uint32_t targetId = net.ServerNet.GetNetId( *target.Get() );
    int checked = 0;
    int same = 0;
    int sameView = 0;
    int onPath = 0;
    int onSurface = 0;
    int hits = 0;
    for( const NetShotComparison& shot : ledger.GetRecent() )
    {
        REQUIRE( shot.HasServer );
        auto it = judged.find( shot.Server.CommandTick );
        REQUIRE( it != judged.end() );
        const Judged& record = it->second;
        const NetReplayShot& result = shot.Server.Shot;
        ++checked;
        bool equal = result.Hit == record.Hit.Hit && result.Occluded == record.Hit.Occluded && SameBits( result.Point, record.Hit.Point )
            && SameBits( result.Distance, record.Hit.Distance ) && result.RewoundTicks == record.Hit.RewoundTicks && result.ViewTick == record.ViewTick
            && result.Target == ( record.Hit.Entity ? targetId : 0u ) && result.Shooter == net.ServerNet.GetNetId( *shooter.Get() );
        // Hit or near miss, the target's one hitbox goes with it.
        equal = equal && result.Hitboxes.size() == 1 && shot.Server.Live.size() == 1 && record.Rewound.size() == 1 && record.Live.size() == 1
            && SamePose( result.Hitboxes[0], record.Rewound[0] ) && SamePose( shot.Server.Live[0], record.Live[0] ) && result.Hitboxes[0].NetId == targetId;
        same += equal ? 1 : 0;
        // The client named the view the server rewound to.
        sameView += shot.HasClient && shot.Client.ViewTick == result.ViewTick ? 1 : 0;
        if( !result.Hitboxes.empty() )
        {
            const Vector3 expected = TargetAtTick( result.ViewTick );
            onPath += ( result.Hitboxes[0].Position - expected ).Length() < 1e-4f ? 1 : 0;
            if( result.Hit )
            {
                ++hits;
                onSurface += std::fabs( FromAxis( result.Point, expected, result.Hitboxes[0].HalfHeight ) - result.Hitboxes[0].Radius ) < 1e-3f ? 1 : 0;
            }
        }
        // The rewind is the round trip's half plus the interpolation delay: several ticks, under the cap.
        CHECK( result.RewoundTicks > 4.0 );
        CHECK( result.RewoundTicks < 24.0 );
        CHECK_FALSE( result.Clamped );
    }
    MESSAGE( checked << " results checked: " << same << " are the server's raycast to the bit, " << sameView << " name the client's view tick, " << onPath
                     << " rewound to the target's path at it; " << onSurface << " of " << hits << " hit points on the rewound capsule" );
    CHECK( checked >= 50 );
    CHECK( same == checked );
    CHECK( sameView == checked );
    CHECK( onPath == checked );
    CHECK( hits * 10 >= checked * 9 );
    CHECK( onSurface == hits );
}


TEST_CASE( "Net: a shotgun's ten results a volley never crowd a near-full snapshot or the owner state out of a packet, and the struck part leads a target of eight (NET-7)" )
{
    PhysicsCore serverPhysics;
    PhysicsCore clientPhysics;
    Harness net( Link( 20.f, 0.f, 95 ), Link( 20.f, 0.f, 96 ) );
    net.ServerWorld->AddCore( serverPhysics );
    net.ClientWorld->AddCore( clientPhysics );
    // Clutter moving every tick, to fill each snapshot to near its budget.
    constexpr uint64_t kClutter = 0xC1000;
    constexpr int kClutterCount = 160;
    for( int i = 0; i < kClutterCount; ++i )
    {
        net.Place( kClutter + i );
    }
    net.Place( 0x7A2608 );     // the target: eight hitboxes on children, the head last
    net.Place( 0x5408 );       // the shooter: the client's predicted pawn
    const Vector3 targetAt( 0.f, 1.f, 10.f );
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        EntityHandle target = Find( *world, 0x7A2608 );
        target->SetName( "Target" );
        Transform& root = target->GetComponent<Transform>();
        root.SetPosition( targetAt );
        for( int i = 0; i < 8; ++i )
        {
            EntityHandle part = world->CreateEntity( i == 7 ? "Head" : "Part" + std::to_string( i ) );
            part->AddComponent<Transform>().SetParent( root );
            NetHitbox& hitbox = part->AddComponent<NetHitbox>();
            if( i == 7 )
            {
                part->GetComponent<Transform>().SetPosition( Vector3( 0.f, 0.9f, 0.f ) );
                hitbox.Radius = 0.3f;
                hitbox.Height = 0.6f;
            }
            else
            {
                // Low boxes, out of the pellets' way.
                part->GetComponent<Transform>().SetPosition( Vector3( -0.6f + 0.2f * i, -0.8f, 0.f ) );
                hitbox.Shape = NetHitboxShape::Box;
                hitbox.HalfExtents = Vector3( 0.08f, 0.15f, 0.1f );
            }
        }
    }
    EntityHandle shooter = Find( *net.ServerWorld, 0x5408 );
    EntityHandle clientShooter = Find( *net.ClientWorld, 0x5408 );
    const Vector3 muzzle( 0.f, 1.9f, 0.f );
    for( EntityHandle pawn : { shooter, clientShooter } )
    {
        pawn->GetComponent<Transform>().SetPosition( muzzle );
        pawn->AddComponent<CharacterController>().GravityScale = 0.f;
    }
    shooter->GetComponent<NetIdentity>().SetOwner( 1 );
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        world->Simulate();
        world->Start();
    }

    // A volley of ten pellets at the head every 15 ticks, the same spread on both sides.
    bool firing = true;
    net.MakeCommand = [&]( uint32_t InTick ) {
        InputCommand command;
        command.HasAim = true;
        command.Buttons = firing && InTick % 15 == 0 ? 2u : 0u;
        return command;
    };
    auto pellet = [&]( int InIndex ) {
        const float angle = InIndex * 0.6283185f;
        return Vector3( std::cos( angle ) * 0.012f, std::sin( angle ) * 0.012f, 1.f );
    };
    size_t judged = 0;
    net.ServerNet.SetCommandHandler( [&]( Entity& InShooter, const InputCommand& InCommand ) {
        if( !( InCommand.Buttons & 2u ) || InCommand.ViewTick < 0.0 )
        {
            return;
        }
        for( int i = 0; i < 10; ++i )
        {
            net.ServerNet.LagCompensatedRaycast( muzzle, pellet( i ), 50.f, InCommand, net.ServerWorld->GetEntity( InShooter.GetId() ) );
            ++judged;
        }
    } );
    net.ClientNet.SetCommandHandler( []( Entity&, const InputCommand& ) {} );
    net.OnClientTick = [&]( NetCore& InNet ) {
        if( InNet.GetPredictor()->GetCommand().Buttons & 2u )
        {
            for( int i = 0; i < 10; ++i )
            {
                InNet.PresentRaycast( muzzle, pellet( i ), 50.f, clientShooter );
            }
        }
        Step( clientPhysics, *net.ClientWorld );
    };
    std::vector<EntityHandle> clutter;
    for( int i = 0; i < kClutterCount; ++i )
    {
        clutter.push_back( Find( *net.ServerWorld, kClutter + i ) );
    }
    net.OnServerTick = [&]( NetServer& InServer ) {
        const float t = InServer.GetTick() / 60.f;
        for( int i = 0; i < kClutterCount; ++i )
        {
            clutter[i]->GetComponent<Transform>().SetPosition( Vector3( 30.f + std::sin( t * 3.f + i ) * 7.f, std::cos( t * 2.f + i ) * 3.f, i * 0.37f ) );
        }
        Step( serverPhysics, *net.ServerWorld );
    };
    NetDebugSubscription debug;
    debug.Shots = true;
    net.ClientNet.SetDebug( debug );
    net.Start();
    net.ClientNet.Predict( clientShooter );
    REQUIRE( net.ClientNet.GetPredictor() );
    net.Run( 3.0 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );

    // While the volleys fly: every snapshot sent arrives, and an owner state every tick.
    NetReplicationServer& replication = *net.ServerNet.GetReplicationServer();
    NetPredictor& predictor = *net.ClientNet.GetPredictor();
    auto reconciled = [&] { return predictor.GetMatchCount() + predictor.GetCorrectionCount() + predictor.GetInitialSyncCount(); };
    const uint64_t sentBefore = replication.GetSnapshotsSent();
    const uint64_t bytesBefore = replication.GetSnapshotBytes();
    const uint64_t receivedBefore = net.ClientNet.GetReplicationClient()->GetSnapshotsReceived();
    const int reconciledBefore = reconciled();
    const uint32_t tickBefore = net.ServerNet.GetServer()->GetTick();
    const size_t judgedBefore = judged;
    net.Run( 8.0 );
    const uint64_t sent = replication.GetSnapshotsSent() - sentBefore;
    const uint64_t received = net.ClientNet.GetReplicationClient()->GetSnapshotsReceived() - receivedBefore;
    const int owners = reconciled() - reconciledBefore;
    const uint32_t ticks = net.ServerNet.GetServer()->GetTick() - tickBefore;
    const double meanSnapshot = sent ? static_cast<double>( replication.GetSnapshotBytes() - bytesBefore ) / static_cast<double>( sent ) : 0.0;
    MESSAGE( ticks << " ticks, " << ( judged - judgedBefore ) << " pellets judged: " << sent << " snapshots sent (" << meanSnapshot << " bytes on average, the budget "
                   << NetReplicationServer::kSnapshotBudget << "), " << received << " received; " << owners << " ticks reconciled from owner state" );
    CHECK( meanSnapshot > NetReplicationServer::kSnapshotBudget * 0.8 );
    CHECK( ticks >= 470 );
    CHECK( sent + 2 >= ticks );
    CHECK( received + 3 >= sent );
    CHECK( owners + 4 >= static_cast<int>( ticks ) );

    // Every result lands (none dropped), the struck part first of the six a result carries.
    firing = false;
    net.Run( 1.5 );
    const NetShotLedger& ledger = net.ClientNet.GetShotLedger();
    const NetShotLedger::Totals& totals = ledger.GetTotals();
    const NetCore::DebugStats& stats = net.ServerNet.GetDebugStats();
    MESSAGE( judged << " pellets judged, " << totals.Results << " results back in " << stats.ShotBundles << " bundles (" << stats.ShotBundleBytes << " bytes), "
                    << totals.Agreed << " of " << totals.Paired << " agree, " << totals.SamePart << " on the same part" );
    CHECK( judged >= 300 );
    CHECK( stats.ShotResultsDropped == 0 );
    CHECK( totals.Results == judged );
    CHECK( totals.Paired == judged );
    CHECK( totals.ClientOnly == 0 );
    CHECK( totals.Agreed * 100 >= totals.Paired * 95 );
    int heads = 0;
    int headFirst = 0;
    int clientHeadFirst = 0;
    for( const NetShotComparison& shot : ledger.GetRecent() )
    {
        if( !shot.HasServer || !shot.Server.Shot.Hit )
        {
            continue;
        }
        ++heads;
        const NetReplayShot& result = shot.Server.Shot;
        headFirst += result.TargetPart == "Head" && result.Hitboxes.size() == NetDebug::kMaxHitboxes && shot.Server.Live.size() == NetDebug::kMaxHitboxes
                && result.Hitboxes[0].Name == "Head" && shot.Server.Live[0].Name == "Head"
            ? 1 : 0;
        clientHeadFirst += shot.HasClient && shot.Client.Hitboxes.size() == NetDebug::kMaxHitboxes && shot.Client.Hitboxes[0].Name == "Head" ? 1 : 0;
    }
    CHECK( heads >= 50 );
    CHECK( headFirst == heads );
    CHECK( clientHeadFirst == heads );
}


TEST_CASE( "Net: per-entity snapshot bytes sum to the snapshots sent, and the client's bandwidth report names the busiest entity first (NET-7)" )
{
    Harness net( Link( 20.f, 0.f, 91 ), Link( 20.f, 0.f, 92 ) );
    net.Place( 0xB051 );   // moves every tick
    net.Place( 0xB052 );   // moves every 15 ticks
    net.Place( 0xB053 );   // never moves
    // Short-lived: they move every tick, then the server destroys them.
    constexpr uint64_t kGone = 0xB0A0;
    for( int i = 0; i < 5; ++i )
    {
        net.Place( kGone + i );
    }
    std::vector<EntityHandle> gone;
    for( int i = 0; i < 5; ++i )
    {
        gone.push_back( Find( *net.ServerWorld, kGone + i ) );
    }
    EntityHandle busy = Find( *net.ServerWorld, 0xB051 );
    EntityHandle slow = Find( *net.ServerWorld, 0xB052 );
    // Placed entities have the same names on both sides (the scene's).
    busy->SetName( "Busy" );
    Find( *net.ClientWorld, 0xB051 )->SetName( "Busy" );
    net.OnServerTick = [&]( NetServer& InServer ) {
        const float t = InServer.GetTick() / 60.f;
        busy->GetComponent<Transform>().SetPosition( Vector3( std::sin( t ) * 5.f, 0.f, std::cos( t ) * 5.f ) );
        if( InServer.GetTick() % 15 == 0 )
        {
            slow->GetComponent<Transform>().SetPosition( Vector3( t, 0.f, 0.f ) );
        }
        for( size_t i = 0; i < gone.size(); ++i )
        {
            if( gone[i] )
            {
                gone[i]->GetComponent<Transform>().SetPosition( Vector3( 0.f, t * ( 1.f + i ), 3.f ) );
            }
        }
        net.ServerWorld->Simulate();
    };
    // The server counts from the start (a --net-report); the client asks for the windows.
    NetDebugSubscription count;
    count.Bandwidth = true;
    net.ServerNet.SetDebug( count );
    net.ClientNet.SetDebug( count );
    net.Start();
    // A second client whose Net Debugger asks for nothing: no window is kept for it.
    Peer& quiet = net.AddClient( Link( 20.f, 0.f, 97 ), [&]( World& InWorld ) {
        for( uint64_t guid : { uint64_t( 0xB051 ), uint64_t( 0xB052 ), uint64_t( 0xB053 ) } )
        {
            EntityHandle entity = InWorld.CreateEntityWithGUID( guid );
            entity->AddComponent<Transform>();
            entity->AddComponent<NetIdentity>();
        }
        for( int i = 0; i < 5; ++i )
        {
            EntityHandle entity = InWorld.CreateEntityWithGUID( kGone + i );
            entity->AddComponent<Transform>();
            entity->AddComponent<NetIdentity>();
        }
    } );
    NetReplicationServer& replication = *net.ServerNet.GetReplicationServer();
    // Few entries kept: past four (and twice the live entities) the gone ones fold into one count.
    replication.Settings.MaxAccountedEntities = 4;
    net.Run( 2.0 );
    for( EntityHandle& entity : gone )
    {
        net.ServerWorld->DestroyEntity( entity );
        entity = EntityHandle();
    }
    net.Run( 2.5 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    REQUIRE( quiet.Net.GetClient()->GetState() == NetClient::State::Connected );

    uint64_t entityBits = replication.GetDespawnedBits();
    for( const auto& [id, bits] : replication.GetEntityBits() )
    {
        entityBits += bits;
    }
    MESSAGE( replication.GetSnapshotsSent() << " snapshots, " << replication.GetSnapshotBytes() << " bytes: " << entityBits << " bits in entity entries, "
        << replication.GetOverheadBits() << " bits of headers" );
    CHECK( replication.GetAccountedBytes() == replication.GetSnapshotBytes() );
    CHECK( entityBits + replication.GetOverheadBits() == replication.GetAccountedBytes() * 8 );
    // The five gone ones folded (they moved every tick for two seconds: plenty of bits), the live three kept.
    CHECK( replication.GetDespawnedBits() > 5u * 60u * 8u );
    CHECK( replication.GetEntityBits().size() == 3 );
    // The quiet client got snapshots, but nothing was counted into a window for it.
    const NetBandwidthReport quietWindow = replication.TakeBandwidthWindow( quiet.Net.GetClient()->GetClientId(), NetDebug::kTopEntities, 4.5 );
    CHECK( quiet.Net.GetReplicationClient()->GetSnapshotsReceived() > 100 );
    CHECK( quietWindow.SnapshotBytes == 0 );
    CHECK( quietWindow.Top.empty() );
    const uint32_t busyId = net.ServerNet.GetNetId( *busy.Get() );
    const uint32_t slowId = net.ServerNet.GetNetId( *slow.Get() );
    const std::vector<NetEntityBytes> top = net.ServerNet.GetTopEntities( 10, 4.5 );
    REQUIRE( top.size() >= 2 );
    CHECK( top[0].NetId == busyId );
    CHECK( top[0].Name == "Busy" );
    CHECK( top[1].NetId == slowId );
    CHECK( top[0].Bits > top[1].Bits * 4 );

    // The client's newest window: a second's worth, the busy one first, and every entity in it
    // (three, all named) plus the headers adding up to the window's bytes.
    const NetBandwidthReport& report = net.ClientNet.GetBandwidthReport();
    MESSAGE( net.ClientNet.GetBandwidthReportsReceived() << " reports; the newest: " << report.SnapshotBytes << " bytes over " << report.Seconds << " s" );
    CHECK( net.ClientNet.GetBandwidthReportsReceived() >= 3 );
    CHECK( report.Seconds == doctest::Approx( 1.0 ) );
    REQUIRE( !report.Top.empty() );
    CHECK( report.Top[0].NetId == busyId );
    CHECK( report.Top[0].Name == "Busy" );     // named from the client's world
    CHECK( report.Top[0].BytesPerSecond > 100.0 );
    uint64_t windowBits = report.OverheadBits;
    for( const NetEntityBytes& entity : report.Top )
    {
        windowBits += entity.Bits;
    }
    CHECK( windowBits == report.SnapshotBytes * 8 );
    CHECK( net.ServerNet.GetDebugStats().ShotResults == 0 );
}


TEST_CASE( "Net: without a Net Debugger nothing is counted, recorded or sent (NET-7)" )
{
    Harness net( Link( 20.f, 0.f, 93 ), Link( 20.f, 0.f, 94 ) );
    net.Place( 0xB061 );
    EntityHandle mover = Find( *net.ServerWorld, 0xB061 );
    EntityHandle clientMover = Find( *net.ClientWorld, 0xB061 );
    clientMover->AddComponent<NetHitbox>();
    net.OnServerTick = [&]( NetServer& InServer ) {
        mover->GetComponent<Transform>().SetPosition( Vector3( InServer.GetTick() * 0.01f, 0.f, 0.f ) );
        net.ServerWorld->Simulate();
    };
    net.OnClientTick = [&]( NetCore& InNet ) { InNet.PresentRaycast( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 1.f ), 50.f ); };
    net.Start();
    net.Run( 2.0 );
    CHECK( net.ServerNet.GetReplicationServer()->GetAccountedBytes() == 0 );
    CHECK( net.ServerNet.GetReplicationServer()->GetEntityBits().empty() );
    CHECK( net.ServerNet.GetDebugStats().Subscriptions == 0 );
    CHECK( net.ServerNet.GetDebugStats().BandwidthReports == 0 );
    CHECK( net.ClientNet.GetShotLedger().GetTotals().Predicted == 0 );
    CHECK( net.ClientNet.GetBandwidthReportsReceived() == 0 );
}


TEST_CASE( "Net: a server that doesn't share with clients' Net Debuggers refuses their subscription and sends them nothing (NET-7)" )
{
    Harness net( Link( 20.f, 0.f, 98 ), Link( 20.f, 0.f, 99 ) );
    net.Place( 0xB071 );
    EntityHandle mover = Find( *net.ServerWorld, 0xB071 );
    net.OnServerTick = [&]( NetServer& InServer ) {
        mover->GetComponent<Transform>().SetPosition( Vector3( InServer.GetTick() * 0.01f, 0.f, 0.f ) );
        net.ServerWorld->Simulate();
    };
    // Debug and editor builds share by default (a Release server needs --net-debug-clients).
    CHECK( net.ServerNet.AreDebugClientsAllowed() == NetDebug::kClientsAllowedByDefault );
    net.ServerNet.SetDebugClientsAllowed( false );
    NetDebugSubscription debug;
    debug.Shots = true;
    debug.Bandwidth = true;
    net.ClientNet.SetDebug( debug );
    net.Start();
    net.Run( 3.0 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    CHECK( net.ServerNet.GetDebugStats().Refused >= 1 );
    CHECK( net.ServerNet.GetDebugStats().Subscriptions == 0 );
    CHECK( net.ServerNet.GetDebugStats().BandwidthReports == 0 );
    CHECK( net.ServerNet.GetReplicationServer()->GetAccountedBytes() == 0 );
    CHECK( net.ClientNet.GetBandwidthReportsReceived() == 0 );
}


TEST_CASE( "Net: shot results round-trip in a bundle and a bandwidth report in its message; a cut one, or another format's, reads as false (NET-7)" )
{
    NetShotResult result;
    result.CommandTick = 1234;
    result.Shot.Client = 2;
    result.Shot.Shooter = 7;
    result.Shot.Origin = Vector3( 1.f, 2.f, 3.f );
    result.Shot.Direction = Vector3( 0.f, 0.f, 1.f );
    result.Shot.Range = 50.f;
    result.Shot.ViewTick = 1220.375;
    result.Shot.RewoundTicks = 11.625;
    result.Shot.Hit = true;
    result.Shot.Target = 9;
    result.Shot.TargetPart = "Head";
    result.Shot.Point = Vector3( 1.f, 2.f, 17.5f );
    result.Shot.Distance = 14.5f;
    NetReplayHitbox hitbox;
    hitbox.NetId = 9;
    hitbox.Name = "Head";
    hitbox.Position = Vector3( 1.f, 2.f, 18.f );
    hitbox.Rotation = Quaternion( 0.f, 0.7071068f, 0.f, 0.7071068f );
    hitbox.Radius = 0.25f;
    result.Shot.Hitboxes.push_back( hitbox );
    hitbox.Position.x += 0.75f;
    result.Live.push_back( hitbox );
    NetShotResult second = result;
    second.CommandTick = 1235;
    second.Shot.Hit = false;
    second.Shot.Hitboxes.clear();
    second.Live.clear();
    const std::vector<uint8_t> bundle = NetDebugMessages::EncodeShots( { result, second } );
    CHECK( NetDebugMessages::IsShotBundle( bundle.data(), bundle.size() ) );
    {
        std::vector<NetShotResult> read;
        REQUIRE( NetDebugMessages::DecodeShots( bundle.data(), bundle.size(), read ) );
        REQUIRE( read.size() == 2 );
        CHECK( read[0].CommandTick == 1234 );
        CHECK( read[0].Shot.ViewTick == 1220.375 );
        CHECK( read[0].Shot.RewoundTicks == 11.625 );
        CHECK( read[0].Shot.TargetPart == "Head" );
        CHECK( SameBits( read[0].Shot.Point, result.Shot.Point ) );
        REQUIRE( read[0].Shot.Hitboxes.size() == 1 );
        REQUIRE( read[0].Live.size() == 1 );
        CHECK( SameBits( read[0].Live[0].Position, hitbox.Position ) );
        CHECK( read[0].Live[0].Rotation.y == hitbox.Rotation.y );
        CHECK( read[1].CommandTick == 1235 );
        CHECK_FALSE( read[1].Shot.Hit );
        CHECK( read[1].Shot.Hitboxes.empty() );
    }
    // Cut short anywhere, it's refused.
    for( size_t cut = 0; cut < bundle.size(); cut += 7 )
    {
        std::vector<NetShotResult> read;
        CHECK_FALSE( NetDebugMessages::DecodeShots( bundle.data(), cut, read ) );
    }
    // A bundle of a replay shot format this build can't read (a server of another engine), or of
    // another bundle version, is refused rather than misread; a killcam blob isn't a bundle at all.
    for( const size_t at : { size_t( 5 ), size_t( 4 ) } )
    {
        for( const uint8_t version : { uint8_t( NetReplayFormat::kVersion + 1 ), uint8_t( NetReplayFormat::kMinVersion - 1 ), uint8_t( 0 ) } )
        {
            std::vector<uint8_t> other = bundle;
            other[at] = at == 5 ? version : static_cast<uint8_t>( NetDebug::kShotBundleVersion + 1 );
            std::vector<NetShotResult> read;
            CHECK_FALSE( NetDebugMessages::DecodeShots( other.data(), other.size(), read ) );
        }
    }
    const uint8_t killcam[] = { 'M', 'K', 'C', 'M', 1, 0, 0, 0, 0 };
    CHECK_FALSE( NetDebugMessages::IsShotBundle( killcam, sizeof( killcam ) ) );

    BitWriter writer;
    NetBandwidthReport report;
    report.Tick = 600;
    report.Seconds = 1.0;
    report.SnapshotBytes = 4000;
    report.OverheadBits = 1600;
    report.Top.push_back( { 3, 24000, 0.0, "" } );
    report.Top.push_back( { 5, 6400, 0.0, "" } );
    writer.Clear();
    NetDebugMessages::WriteBandwidth( writer, report );
    BitReader reader( writer.GetBytes() );
    reader.ReadBits( 8 );
    uint8_t kind = 0;
    NetBandwidthReport read;
    REQUIRE( NetDebugMessages::ReadDebug( reader, kind, read ) );
    CHECK( kind == NetDebug::Bandwidth );
    REQUIRE( read.Top.size() == 2 );
    CHECK( read.Top[0].NetId == 3 );
    CHECK( read.Top[0].BytesPerSecond == doctest::Approx( 3000.0 ) );
    CHECK( read.OverheadBits == 1600 );
}
