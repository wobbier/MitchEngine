#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetHitbox.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/Colliders.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Net/NetCommands.h"
#include "Net/NetLagCompensation.h"
#include "NetHarness.h"
#include <cmath>
#include <map>

namespace NetLagTest
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

    // Strafes along X between -4 and 4 at 5 m/s.
    Vector3 TargetAt( uint32_t InTick )
    {
        const float distance = 5.f * InTick / 60.f;
        const float phase = std::fmod( distance, 16.f );
        const float x = phase < 8.f ? phase - 4.f : 12.f - phase;
        return Vector3( x, 1.f, 10.f );
    }
}

using namespace NetLagTest;
using namespace NetHarness;

TEST_CASE( "Net: hitbox rays hit capsules and oriented boxes where they are, and nowhere else" )
{
    NetLagCompensation::HitboxPose capsule;
    capsule.Shape = static_cast<uint8_t>( NetHitboxShape::Capsule );
    capsule.Position = Vector3( 0.f, 1.f, 5.f );
    capsule.Radius = 0.4f;
    capsule.HalfHeight = 0.5f;
    CHECK( NetLagCompensation::Intersect( capsule, Vector3( 0.f, 1.f, 0.f ), Vector3( 0.f, 0.f, 1.f ), 100.f ) == doctest::Approx( 4.6f ) );
    CHECK( NetLagCompensation::Intersect( capsule, Vector3( 0.f, 1.85f, 0.f ), Vector3( 0.f, 0.f, 1.f ), 100.f ) > 4.6f );   // the cap
    CHECK( NetLagCompensation::Intersect( capsule, Vector3( 0.45f, 1.f, 0.f ), Vector3( 0.f, 0.f, 1.f ), 100.f ) < 0.f );
    CHECK( NetLagCompensation::Intersect( capsule, Vector3( 0.f, 1.f, 0.f ), Vector3( 0.f, 0.f, 1.f ), 3.f ) < 0.f );       // out of range
    CHECK( NetLagCompensation::Intersect( capsule, Vector3( 0.f, 1.f, 0.f ), Vector3( 0.f, 0.f, -1.f ), 100.f ) < 0.f );     // behind

    NetLagCompensation::HitboxPose box;
    box.Shape = static_cast<uint8_t>( NetHitboxShape::Box );
    box.Position = Vector3( 0.f, 0.f, 5.f );
    box.Rotation = Quaternion::FromEulerDegrees( Vector3( 0.f, 45.f, 0.f ) );
    box.HalfExtents = Vector3( 1.f, 1.f, 1.f );
    // Turned 45 degrees: its corner faces the ray, sqrt(2) out from the centre.
    CHECK( NetLagCompensation::Intersect( box, Vector3( 0.f, 0.f, 0.f ), Vector3( 0.f, 0.f, 1.f ), 100.f ) == doctest::Approx( 5.f - std::sqrt( 2.f ) ).epsilon( 0.001 ) );
    CHECK( NetLagCompensation::Intersect( box, Vector3( 1.3f, 0.f, 0.f ), Vector3( 0.f, 0.f, 1.f ), 100.f ) >= 0.f );
    CHECK( NetLagCompensation::Intersect( box, Vector3( 1.5f, 0.f, 0.f ), Vector3( 0.f, 0.f, 1.f ), 100.f ) < 0.f );
}

TEST_CASE( "Net: at 200 ms a shot at a strafing target hits with rewind and misses without; beyond the cap is clamped; walls still block" )
{
    PhysicsCore physics;
    Harness net( Link( 100.f, 5.f, 51 ), Link( 100.f, 5.f, 52 ) );
    net.ServerWorld->AddCore( physics );
    net.Place( 0x7A26E7 );     // the target, mirrored to the client
    net.Place( 0x5400 );       // the shooter, owned by the client
    EntityHandle target;
    EntityHandle shooter;
    EntityHandle clientTarget;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E7 ) target = net.ServerWorld->GetEntity( InEntity.GetId() );
        if( InEntity.GetGUID() == 0x5400 ) shooter = net.ServerWorld->GetEntity( InEntity.GetId() );
    } );
    net.ClientWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E7 ) clientTarget = net.ClientWorld->GetEntity( InEntity.GetId() );
    } );
    NetHitbox& hitbox = target->AddComponent<NetHitbox>();
    hitbox.Radius = 0.4f;
    hitbox.Height = 1.8f;
    shooter->GetComponent<Transform>().SetPosition( Vector3( 0.f, 1.f, -10.f ) );
    shooter->GetComponent<NetIdentity>().SetOwner( 1 );
    // A wall the shooter can't see through, off to one side (only some shots cross it).
    EntityHandle wall = net.ServerWorld->CreateEntity( "Wall" );
    wall->AddComponent<Transform>().SetPosition( Vector3( 2.5f, 1.f, 5.f ) );
    wall->AddComponent<BoxCollider>().Size = Vector3( 1.f, 4.f, 0.2f );
    net.ServerWorld->Simulate();
    net.ServerWorld->Start();

    // The client aims at the target where it sees it (interpolated), firing every 20 ticks.
    const Vector3 muzzle( 0.f, 1.f, -10.f );
    net.MakeCommand = [&]( uint32_t InTick ) {
        InputCommand command;
        const Vector3 aim = ( clientTarget->GetComponent<Transform>().GetWorldPosition() - muzzle ).Normalized();
        command.Axes = { aim.x, aim.y, aim.z, 0.f };
        command.Buttons = InTick % 20 == 0 ? 2u : 0u;
        return command;
    };
    int shots = 0;
    int rewoundHits = 0;
    int presentHits = 0;
    int rejectedForged = 0;
    int occluded = 0;
    double rewound = 0.0;
    net.ServerNet.SetCommandHandler( [&]( Entity& InShooter, const InputCommand& InCommand ) {
        if( !( InCommand.Buttons & 2u ) || InCommand.ViewTick < 0.0 )
        {
            return;
        }
        const Vector3 direction( InCommand.Axes[0], InCommand.Axes[1], InCommand.Axes[2] );
        const EntityHandle self = net.ServerWorld->GetEntity( InShooter.GetId() );
        const LagCompensatedHit withRewind = net.ServerNet.LagCompensatedRaycast( muzzle, direction, 100.f, InCommand, self );
        InputCommand present = InCommand;
        present.ViewTick = -1.0;
        const LagCompensatedHit withoutRewind = net.ServerNet.LagCompensatedRaycast( muzzle, direction, 100.f, present, self );
        InputCommand forged = InCommand;
        forged.ViewTick -= 30.0;   // half a second further back than it really saw
        const LagCompensatedHit forgedHit = net.ServerNet.LagCompensatedRaycast( muzzle, direction, 100.f, forged, self );
        // NET-15: judged at the cap rather than refused.
        rejectedForged += forgedHit.Clamped && !forgedHit.Rejected && forgedHit.RewoundTicks <= 0.4 * 60.0 + 1e-6 ? 1 : 0;
        ++shots;
        rewoundHits += withRewind.Hit && withRewind.Entity == target ? 1 : 0;
        presentHits += withoutRewind.Hit ? 1 : 0;
        occluded += withRewind.Occluded ? 1 : 0;
        rewound += withRewind.RewoundTicks;
    } );
    net.OnServerTick = [&]( NetServer& InServer ) {
        target->GetComponent<Transform>().SetPosition( TargetAt( InServer.GetTick() ) );
        TestContext context;
        context.FixedDeltaTime = 1.f / 60.f;
        context.InterpolationAlpha = 1.f;
        net.ServerWorld->Simulate();
        context.IsFixedStepActive = true;
        physics.FixedUpdate( context );
        context.IsFixedStepActive = false;
        physics.Update( context );
    };
    net.Start();
    net.Run( 2.0 );     // connect; the view settles
    shots = rewoundHits = presentHits = rejectedForged = occluded = 0;
    rewound = 0.0;
    net.Run( 20.0 );

    MESSAGE( shots << " shots: " << rewoundHits << " hit with rewind (" << occluded << " behind the wall), " << presentHits
        << " without; rewound " << ( shots ? rewound / shots : 0.0 ) << " ticks on average" );
    REQUIRE( shots >= 50 );
    // Rewound to what the shooter saw, every unobstructed shot lands; against the present the
    // target has moved over a metre on.
    CHECK( occluded > 0 );
    CHECK( rewoundHits + occluded == shots );
    CHECK( presentHits <= shots / 10 );
    CHECK( rejectedForged == shots );
    CHECK( rewound / shots > 12.0 );   // 200 ms round trip and then some
}


TEST_CASE( "Net: the rewind broadphase gives the same hits while testing under 10% of 200 hitboxes (NET-15)" )
{
    std::shared_ptr<World> world = std::make_shared<World>();
    world->IsLoading = false;
    std::vector<EntityHandle> targets;
    for( int i = 0; i < 200; ++i )
    {
        EntityHandle target = world->CreateEntity( "Target" );
        target->AddComponent<Transform>();
        NetHitbox& hitbox = target->AddComponent<NetHitbox>();
        hitbox.Radius = 0.4f;
        hitbox.Height = 1.8f;
        if( i % 4 == 0 )
        {
            hitbox.Shape = NetHitboxShape::Box;
            hitbox.HalfExtents = Vector3( 0.5f, 0.9f, 0.3f );
        }
        targets.push_back( target );
    }
    // A 20 x 10 grid, 4 m apart, each strafing its own way.
    auto place = [&]( uint32_t InTick ) {
        for( int i = 0; i < 200; ++i )
        {
            const float sway = std::sin( ( InTick + i * 7 ) * 0.05f ) * 1.5f;
            targets[i]->GetComponent<Transform>().SetPosition( Vector3( ( i % 20 ) * 4.f + sway, 1.f, ( i / 20 ) * 4.f + sway * 0.5f ) );
        }
    };
    NetLagCompensation lag;
    for( uint32_t tick = 1; tick <= 30; ++tick )
    {
        place( tick );
        lag.Record( *world, tick );
    }

    int shots = 0;
    int hits = 0;
    int tested = 0;
    for( int shot = 0; shot < 200; ++shot )
    {
        // From a step back, aimed at a target's rough spot, or off into the gaps.
        const int aimAt = ( shot * 37 ) % 200;
        const Vector3 origin( ( aimAt % 20 ) * 4.f - 3.f, 1.2f, -8.f );
        const Vector3 at( ( aimAt % 20 ) * 4.f + ( shot % 3 ) * 0.7f, 1.f + ( shot % 5 ) * 0.2f, ( aimAt / 20 ) * 4.f );
        const double viewTick = 30.0 - ( shot % 20 ) * 0.75;
        lag.UseBroadphase = false;
        const LagCompensatedHit everything = lag.Raycast( nullptr, origin, at - origin, 200.f, viewTick );
        lag.UseBroadphase = true;
        const LagCompensatedHit culled = lag.Raycast( nullptr, origin, at - origin, 200.f, viewTick );
        CAPTURE( shot );
        CHECK( everything.Tested == 200 );
        CHECK( culled.Hit == everything.Hit );
        CHECK( culled.Entity == everything.Entity );
        CHECK( culled.Distance == everything.Distance );
        CHECK( culled.Point.x == everything.Point.x );
        CHECK( culled.Point.y == everything.Point.y );
        CHECK( culled.Point.z == everything.Point.z );
        ++shots;
        hits += culled.Hit ? 1 : 0;
        tested += culled.Tested;
    }
    const double perShot = static_cast<double>( tested ) / shots;
    MESSAGE( "broadphase: " << hits << "/" << shots << " hits, " << perShot << " of 200 hitboxes tested per shot" );
    CHECK( hits > 50 );
    CHECK( perShot < 20.0 );
}


TEST_CASE( "Net: a moving door blocks shots where it was in the shooter's view, not where it is now (NET-15)" )
{
    // 200 ms of round trip: the shooter's view is about 12 ticks behind the server's newest.
    std::shared_ptr<World> world = std::make_shared<World>();
    world->IsLoading = false;
    EntityHandle target = world->CreateEntity( "Target" );
    target->AddComponent<Transform>().SetPosition( Vector3( 0.f, 1.f, 10.f ) );
    target->AddComponent<NetHitbox>();
    EntityHandle door = world->CreateEntity( "Door" );
    door->AddComponent<Transform>();
    NetHitbox& doorBox = door->AddComponent<NetHitbox>();
    doorBox.Shape = NetHitboxShape::Box;
    doorBox.HalfExtents = Vector3( 1.f, 1.5f, 0.1f );
    doorBox.Occluder = true;
    // The door slides across the line of fire at tick 46 (open: 3 m aside), and back at tick 76.
    auto doorAt = []( uint32_t InTick ) { return Vector3( InTick >= 46 && InTick < 76 ? 0.f : 3.f, 1.f, 5.f ); };
    NetLagCompensation lag;
    const Vector3 muzzle( 0.f, 1.f, 0.f );
    const Vector3 aim( 0.f, 0.f, 1.f );
    LagCompensatedHit doorOpenInView;
    LagCompensatedHit doorClosedInView;
    LagCompensatedHit present;
    for( uint32_t tick = 1; tick <= 90; ++tick )
    {
        door->GetComponent<Transform>().SetPosition( doorAt( tick ) );
        lag.Record( *world, tick );
        if( tick == 52 )
        {
            // Closed now (since 46, 100 ms ago), but the shooter saw tick 40, still open: a hit.
            doorOpenInView = lag.Raycast( nullptr, muzzle, aim, 100.f, 40.0 );
            present = lag.Raycast( nullptr, muzzle, aim, 100.f, -1.0 );
        }
        if( tick == 82 )
        {
            // Open again now (since 76), but the shooter saw tick 70, closed: blocked.
            doorClosedInView = lag.Raycast( nullptr, muzzle, aim, 100.f, 70.0 );
        }
    }
    CHECK( doorOpenInView.Hit );
    CHECK( doorOpenInView.Entity == target );
    CHECK_FALSE( doorOpenInView.Occluded );
    CHECK_FALSE( present.Hit );         // without rewind the closed door is in the way
    CHECK( present.Occluded );
    CHECK_FALSE( doorClosedInView.Hit );
    CHECK( doorClosedInView.Occluded );
    CHECK( doorClosedInView.Entity == target );
}


TEST_CASE( "Net: at 600 ms the client extrapolates within the rewind cap; no shot is rejected, and reversals are the misses (NET-15)" )
{
    PhysicsCore physics;
    Harness net( Link( 300.f, 5.f, 61 ), Link( 300.f, 5.f, 62 ) );
    net.ServerWorld->AddCore( physics );
    net.Place( 0x7A26E7 );
    net.Place( 0x5400 );
    EntityHandle target;
    EntityHandle shooter;
    EntityHandle clientTarget;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E7 ) target = net.ServerWorld->GetEntity( InEntity.GetId() );
        if( InEntity.GetGUID() == 0x5400 ) shooter = net.ServerWorld->GetEntity( InEntity.GetId() );
    } );
    net.ClientWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E7 ) clientTarget = net.ClientWorld->GetEntity( InEntity.GetId() );
    } );
    NetHitbox& hitbox = target->AddComponent<NetHitbox>();
    hitbox.Radius = 0.4f;
    hitbox.Height = 1.8f;
    shooter->GetComponent<Transform>().SetPosition( Vector3( 0.f, 1.f, -10.f ) );
    shooter->GetComponent<NetIdentity>().SetOwner( 1 );
    net.ServerWorld->Simulate();
    net.ServerWorld->Start();

    const Vector3 muzzle( 0.f, 1.f, -10.f );
    double errorSum = 0.0;
    int errorCount = 0;
    double aheadSum = 0.0;
    net.MakeCommand = [&]( uint32_t InTick ) {
        InputCommand command;
        const Vector3 shown = clientTarget->GetComponent<Transform>().GetWorldPosition();
        const double render = net.ClientNet.GetReplicationClient()->GetRenderTick();
        const float x0 = TargetAt( static_cast<uint32_t>( render ) ).x;
        const float x1 = TargetAt( static_cast<uint32_t>( render ) + 1 ).x;
        const float expected = x0 + ( x1 - x0 ) * static_cast<float>( render - std::floor( render ) );
        errorSum += std::abs( shown.x - expected );
        aheadSum += render - net.ClientNet.GetReplicationClient()->GetNewestSnapshotTick();
        ++errorCount;
        const Vector3 aim = ( shown - muzzle ).Normalized();
        command.Axes = { aim.x, aim.y, aim.z, 0.f };
        command.Buttons = InTick % 20 == 0 ? 2u : 0u;
        return command;
    };
    int shots = 0;
    int hits = 0;
    int rejected = 0;
    int clamped = 0;
    int missesAwayFromReversals = 0;
    double worstRewind = 0.0;
    net.ServerNet.SetCommandHandler( [&]( Entity& InShooter, const InputCommand& InCommand ) {
        if( !( InCommand.Buttons & 2u ) || InCommand.ViewTick < 0.0 )
        {
            return;
        }
        const Vector3 direction( InCommand.Axes[0], InCommand.Axes[1], InCommand.Axes[2] );
        const LagCompensatedHit hit = net.ServerNet.LagCompensatedRaycast( muzzle, direction, 100.f, InCommand, net.ServerWorld->GetEntity( InShooter.GetId() ) );
        ++shots;
        hits += hit.Hit ? 1 : 0;
        rejected += hit.Rejected ? 1 : 0;
        clamped += hit.Clamped ? 1 : 0;
        worstRewind = std::max( worstRewind, hit.RewoundTicks );
        if( !hit.Hit )
        {
            // Extrapolation can only be wrong where the target turned: within 0.6 s of an end.
            const float phase = std::fmod( 5.f * static_cast<float>( InCommand.ViewTick ) / 60.f, 8.f );
            missesAwayFromReversals += phase > 3.f && phase < 5.f ? 1 : 0;
        }
    } );
    net.OnServerTick = [&]( NetServer& InServer ) {
        target->GetComponent<Transform>().SetPosition( TargetAt( InServer.GetTick() ) );
        TestContext context;
        context.FixedDeltaTime = 1.f / 60.f;
        context.InterpolationAlpha = 1.f;
        net.ServerWorld->Simulate();
        context.IsFixedStepActive = true;
        physics.FixedUpdate( context );
        context.IsFixedStepActive = false;
        physics.Update( context );
    };
    net.Start();
    net.Run( 3.0 );
    shots = hits = rejected = clamped = missesAwayFromReversals = 0;
    worstRewind = 0.0;
    errorSum = aheadSum = 0.0;
    errorCount = 0;
    net.Run( 25.0 );
    MESSAGE( "600 ms: shown vs server x error " << errorSum / std::max( errorCount, 1 ) << " m, render ahead of newest sample " << aheadSum / std::max( errorCount, 1 ) << " ticks" );
    MESSAGE( "600 ms: " << shots << " shots, " << hits << " hits, " << clamped << " clamped, worst rewind " << worstRewind
        << " ticks, extrapolating " << net.ClientNet.GetReplicationClient()->IsExtrapolating() );
    REQUIRE( shots >= 50 );
    // Never refused: views past the cap are judged at the cap.
    CHECK( rejected == 0 );
    CHECK( clamped > 0 );
    CHECK( worstRewind <= 0.4 * 60.0 + 1e-6 );
    CHECK( net.ClientNet.GetReplicationClient()->IsExtrapolating() );
    // What the client showed was a fair dead-reckoning of where the target was at its view tick.
    CHECK( errorSum / std::max( errorCount, 1 ) < 0.25 );
    CHECK( hits > 0 );
    CHECK( missesAwayFromReversals <= shots / 10 );
}


TEST_CASE( "Net: a client's present-view raycast tests its hitboxes where it shows them; round trips read on both ends (NET-15)" )
{
    Harness net( Link( 50.f, 0.f, 61 ), Link( 50.f, 0.f, 62 ) );
    net.Place( 0x7A26E8 );     // the target, mirrored to the client
    EntityHandle target;
    EntityHandle clientTarget;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E8 ) target = net.ServerWorld->GetEntity( InEntity.GetId() );
    } );
    net.ClientWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E8 ) clientTarget = net.ClientWorld->GetEntity( InEntity.GetId() );
    } );
    // The client's copy has its hitbox too (scenes put them on both sides).
    NetHitbox& hitbox = clientTarget->AddComponent<NetHitbox>();
    hitbox.Radius = 0.4f;
    hitbox.Height = 1.8f;
    net.ClientWorld->Simulate();
    net.OnServerTick = [&]( NetServer& InServer ) {
        target->GetComponent<Transform>().SetPosition( TargetAt( InServer.GetTick() ) );
        net.ServerWorld->Simulate();
    };
    net.Start();
    net.Run( 2.0 );

    // Where the client shows the target now (interpolated, ~100 ms behind the server's).
    const Vector3 muzzle( 0.f, 1.f, -10.f );
    const Vector3 shown = clientTarget->GetComponent<Transform>().GetWorldPosition();
    const LagCompensatedHit hit = net.ClientNet.PresentRaycast( muzzle, shown - muzzle, 100.f );
    CHECK( hit.Hit );
    CHECK( hit.Entity == clientTarget );
    CHECK( hit.RewoundTicks == 0.0 );
    CHECK_FALSE( hit.Rejected );
    CHECK_FALSE( net.ClientNet.PresentRaycast( muzzle, shown + Vector3( 2.f, 0.f, 0.f ) - muzzle, 100.f ).Hit );

    // 50 ms each way.
    MESSAGE( "round trip: client " << net.ClientNet.GetRoundTripMs() << " ms, server to client 1 " << net.ServerNet.GetRoundTripMs( 1 ) << " ms" );
    CHECK( net.ClientNet.GetRoundTripMs() == doctest::Approx( 100.f ).epsilon( 0.25 ) );
    CHECK( net.ServerNet.GetRoundTripMs( 1 ) == doctest::Approx( 100.f ).epsilon( 0.25 ) );
    CHECK( net.ServerNet.GetRoundTripMs( 9 ) == 0.f );
}


TEST_CASE( "Net: at 150 ms a client aiming with its command's yaw and pitch hits a strafing target; the server's aim is the client's to the bit (NET-15)" )
{
    PhysicsCore physics;
    Harness net( Link( 75.f, 5.f, 71 ), Link( 75.f, 5.f, 72 ) );
    net.ServerWorld->AddCore( physics );
    net.Place( 0x7A26E9 );     // the target, mirrored to the client
    net.Place( 0x5401 );       // the shooter, owned by the client
    EntityHandle target;
    EntityHandle shooter;
    EntityHandle clientTarget;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E9 ) target = net.ServerWorld->GetEntity( InEntity.GetId() );
        if( InEntity.GetGUID() == 0x5401 ) shooter = net.ServerWorld->GetEntity( InEntity.GetId() );
    } );
    net.ClientWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E9 ) clientTarget = net.ClientWorld->GetEntity( InEntity.GetId() );
    } );
    NetHitbox& hitbox = target->AddComponent<NetHitbox>();
    hitbox.Radius = 0.4f;
    hitbox.Height = 1.8f;
    shooter->GetComponent<Transform>().SetPosition( Vector3( 0.f, 1.f, -10.f ) );
    shooter->GetComponent<NetIdentity>().SetOwner( 1 );
    net.ServerWorld->Simulate();
    net.ServerWorld->Start();

    // The client turns its view onto the target where it shows it (as a mouse would) and fires
    // every 20 ticks; the aim travels as absolute yaw / pitch, nothing in the axes.
    const Vector3 muzzle( 0.f, 1.f, -10.f );
    std::map<uint32_t, std::pair<float, float>> sent;
    net.MakeCommand = [&]( uint32_t InTick ) {
        InputCommand command;
        const Vector3 to = clientTarget->GetComponent<Transform>().GetWorldPosition() + Vector3( 0.f, 0.3f, 0.f ) - muzzle;
        command.HasAim = true;
        command.Yaw = std::atan2( to.x, to.z ) * 180.f / 3.14159265f;
        command.Pitch = std::atan2( to.y, std::sqrt( to.x * to.x + to.z * to.z ) ) * 180.f / 3.14159265f;
        command.Buttons = InTick % 20 == 0 ? 2u : 0u;
        sent[InTick] = { NetCommands::QuantizeYaw( command.Yaw ), NetCommands::QuantizePitch( command.Pitch ) };
        return command;
    };
    int shots = 0;
    int hits = 0;
    int sameAim = 0;
    net.ServerNet.SetCommandHandler( [&]( Entity& InShooter, const InputCommand& InCommand ) {
        if( !( InCommand.Buttons & 2u ) || InCommand.ViewTick < 0.0 || !InCommand.HasAim )
        {
            return;
        }
        const float yaw = InCommand.Yaw * 3.14159265f / 180.f;
        const float pitch = InCommand.Pitch * 3.14159265f / 180.f;
        const Vector3 direction( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
        const LagCompensatedHit hit = net.ServerNet.LagCompensatedRaycast( muzzle, direction, 100.f, InCommand, net.ServerWorld->GetEntity( InShooter.GetId() ) );
        ++shots;
        hits += hit.Hit && hit.Entity == target ? 1 : 0;
        auto client = sent.find( InCommand.Tick );
        sameAim += client != sent.end() && client->second.first == InCommand.Yaw && client->second.second == InCommand.Pitch ? 1 : 0;
    } );
    net.OnServerTick = [&]( NetServer& InServer ) {
        target->GetComponent<Transform>().SetPosition( TargetAt( InServer.GetTick() ) + Vector3( 0.f, 0.f, 10.f ) );
        TestContext context;
        context.FixedDeltaTime = 1.f / 60.f;
        context.InterpolationAlpha = 1.f;
        net.ServerWorld->Simulate();
        context.IsFixedStepActive = true;
        physics.FixedUpdate( context );
        context.IsFixedStepActive = false;
        physics.Update( context );
    };
    net.Start();
    net.Run( 2.0 );
    shots = hits = sameAim = 0;
    net.Run( 20.0 );

    MESSAGE( shots << " shots aimed by the command: " << hits << " hit, " << sameAim << " with the client's aim to the bit" );
    REQUIRE( shots >= 50 );
    CHECK( hits * 100 >= shots * 95 );
    CHECK( sameAim == shots );
}
