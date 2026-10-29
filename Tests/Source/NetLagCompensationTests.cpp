#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetHitbox.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/Colliders.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Net/NetLagCompensation.h"
#include "NetHarness.h"
#include <cmath>

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

TEST_CASE( "Net: at 200 ms a shot at a strafing target hits with rewind and misses without; beyond the cap is rejected; walls still block" )
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
        rejectedForged += net.ServerNet.LagCompensatedRaycast( muzzle, direction, 100.f, forged, self ).Rejected ? 1 : 0;
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
