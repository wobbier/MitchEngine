#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Net/NetPrediction.h"
#include "NetHarness.h"
#include <cstring>
#include <map>

// NET-6 without .NET: a C++ stand-in for the C# glue (what ScriptNet.cpp does with real scripts),
// so the engine side of script state, ownership and RPCs is proven in two worlds.
namespace NetScriptTest
{
    constexpr uint64_t kPawn = 0x5C01;
    constexpr uint64_t kCrate = 0x5C02;
    constexpr uint32_t kDash = 2u;     // the "Dash" button bit

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

    NetConditions Link( float InLatencyMs, uint64_t InSeed )
    {
        NetConditions conditions;
        conditions.LatencyMs = InLatencyMs;
        conditions.JitterMs = 2.f;
        conditions.Seed = InSeed;
        return conditions;
    }

    struct Rpc
    {
        uint64_t Entity = 0;
        int Id = 0;
        std::vector<uint8_t> Data;
    };

    // One world's "scripts": a predicted counter on the pawn (+1 a tick, +10 when Dash goes down),
    // a replicated value per entity, and the RPCs that arrived.
    struct FakeScripts
    {
        NetCore* Net = nullptr;
        std::map<uint64_t, int32_t> Predicted;
        std::map<uint64_t, int32_t> Replicated;
        std::map<uint64_t, bool> ReadAsOwned;
        std::vector<Rpc> Rpcs;
        int ResimulatedTicks = 0;

        static void Append( std::vector<uint8_t>& OutBytes, int32_t InValue )
        {
            uint8_t bytes[4];
            std::memcpy( bytes, &InValue, 4 );
            OutBytes.insert( OutBytes.end(), bytes, bytes + 4 );
        }

        static int32_t Read( const std::vector<uint8_t>& InBytes )
        {
            int32_t value = 0;
            if( InBytes.size() >= 4 )
            {
                std::memcpy( &value, InBytes.data(), 4 );
            }
            return value;
        }

        // The pawn's script: one fixed step with whatever command drives it.
        void Simulate( Entity& InPawn )
        {
            InputCommand command;
            InputCommand previous;
            if( !Net->GetCommand( InPawn, command, previous ) )
            {
                return;
            }
            int32_t& counter = Predicted[InPawn.GetGUID()];
            counter += 1;
            if( ( command.Buttons & kDash ) && !( previous.Buttons & kDash ) )
            {
                counter += 10;
            }
            ResimulatedTicks += Net->IsResimulating() ? 1 : 0;
        }

        NetCore::ScriptGlue Glue()
        {
            NetCore::ScriptGlue glue;
            glue.State.WriteReplicated = [this]( Entity& InEntity, std::vector<uint8_t>& OutBytes ) {
                auto it = Replicated.find( InEntity.GetGUID() );
                if( it != Replicated.end() )
                {
                    Append( OutBytes, it->second );
                }
            };
            glue.State.ReadReplicated = [this]( Entity& InEntity, const std::vector<uint8_t>& InBytes, bool InOwned ) {
                Replicated[InEntity.GetGUID()] = Read( InBytes );
                ReadAsOwned[InEntity.GetGUID()] = InOwned;
            };
            glue.State.SavePredicted = [this]( Entity& InEntity, std::vector<uint8_t>& OutBytes ) {
                if( InEntity.GetGUID() == kPawn )
                {
                    Append( OutBytes, Predicted[kPawn] );
                }
            };
            glue.RestorePredicted = [this]( Entity& InEntity, const std::vector<uint8_t>& InBytes ) {
                Predicted[InEntity.GetGUID()] = Read( InBytes );
            };
            glue.SimulatePredicted = [this]( Entity& InEntity ) { Simulate( InEntity ); };
            glue.DispatchRpc = [this]( Entity& InEntity, int InId, const uint8_t* InData, size_t InSize ) {
                Rpcs.push_back( Rpc{ InEntity.GetGUID(), InId, std::vector<uint8_t>( InData, InData + InSize ) } );
            };
            glue.SchemaHash = []() { return uint64_t( 0x5C5C ); };
            return glue;
        }
    };

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

    void Floor( World& InWorld )
    {
        EntityHandle floor = InWorld.CreateEntity();
        floor->AddComponent<Transform>().SetPosition( Vector3( 0.f, -0.5f, 0.f ) );
        floor->AddComponent<BoxCollider>().Size = Vector3( 40.f, 1.f, 40.f );
    }
}

using namespace NetScriptTest;
using namespace NetHarness;

TEST_CASE( "Net: scripts' state, owners and RPCs ride the session; a dash predicts with 0 corrections, a server-only change is exactly 1 (NET-6)" )
{
    PhysicsCore serverPhysics;
    PhysicsCore clientPhysics;
    Harness net( Link( 40.f, 61 ), Link( 40.f, 62 ) );
    net.ServerWorld->AddCore( serverPhysics );
    net.ClientWorld->AddCore( clientPhysics );
    Floor( *net.ServerWorld );
    Floor( *net.ClientWorld );
    net.Place( kPawn );
    net.Place( kCrate );
    for( World* world : { net.ServerWorld.get(), net.ClientWorld.get() } )
    {
        EntityHandle pawn = Find( *world, kPawn );
        pawn->GetComponent<Transform>().SetPosition( Vector3( 0.f, 0.9f, 0.f ) );
        pawn->AddComponent<CharacterController>();
        world->Simulate();
        world->Start();
    }
    EntityHandle serverPawn = Find( *net.ServerWorld, kPawn );
    EntityHandle clientPawn = Find( *net.ClientWorld, kPawn );
    EntityHandle serverCrate = Find( *net.ServerWorld, kCrate );
    EntityHandle clientCrate = Find( *net.ClientWorld, kCrate );

    FakeScripts server;
    FakeScripts client;
    server.Net = &net.ServerNet;
    client.Net = &net.ClientNet;
    server.Replicated[kCrate] = 7;
    server.Replicated[kPawn] = 100;
    net.ServerNet.SetScriptGlue( server.Glue() );
    net.ClientNet.SetScriptGlue( client.Glue() );

    // Dash goes down for 3 ticks every 90.
    net.MakeCommand = []( uint32_t InTick ) {
        InputCommand command;
        command.Buttons = InTick % 90 < 3 ? kDash : 0u;
        return command;
    };
    int bonus = 0;
    int bonusTick = -1;
    net.OnServerTick = [&]( NetServer& InServer ) {
        server.Simulate( *serverPawn.Get() );
        if( bonusTick >= 0 && InServer.GetTick() == static_cast<uint32_t>( bonusTick ) )
        {
            server.Predicted[kPawn] += 1000;   // something only the server knew
            ++bonus;
        }
        Step( serverPhysics, *net.ServerWorld );
    };
    net.OnClientTick = [&]( NetCore& InNet ) {
        if( InNet.IsPredicted( *clientPawn.Get() ) )
        {
            client.Simulate( *clientPawn.Get() );
        }
        Step( clientPhysics, *net.ClientWorld );
    };
    net.Start();
    net.Run( 0.5 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    CHECK_FALSE( net.ClientNet.IsPredicted( *clientPawn.Get() ) );
    // Unowned networked scripts don't simulate on a client; everything does on the server.
    CHECK_FALSE( net.ClientNet.SimulatesLocally( *clientPawn.Get() ) );
    CHECK( net.ServerNet.SimulatesLocally( *serverPawn.Get() ) );

    // The server hands the pawn to the client, which starts predicting it.
    net.ServerNet.SetOwner( *serverPawn.Get(), net.ClientNet.GetClient()->GetClientId() );
    net.Run( 0.5 );
    CHECK( clientPawn->GetComponent<NetIdentity>().GetOwner() == net.ClientNet.GetClient()->GetClientId() );
    REQUIRE( net.ClientNet.IsPredicted( *clientPawn.Get() ) );
    CHECK( net.ClientNet.SimulatesLocally( *clientPawn.Get() ) );
    CHECK( net.ServerNet.GetOwner( *serverPawn.Get() ) == 1 );

    // RPCs: up from the owner, down to the owner and to everyone; a client can't call on what it
    // doesn't own.
    const std::vector<uint8_t> payload = { 1, 2, 3 };
    CHECK( net.ClientNet.SendRpc( *clientPawn.Get(), 3, NetCore::RpcKind::ToServer, payload.data(), payload.size() ) );
    CHECK( net.ClientNet.SendRpc( *clientCrate.Get(), 4, NetCore::RpcKind::ToServer, payload.data(), payload.size() ) );
    CHECK_FALSE( net.ClientNet.SendRpc( *clientPawn.Get(), 5, NetCore::RpcKind::ToOwner, payload.data(), payload.size() ) );
    const std::vector<uint8_t> down = { 9 };
    CHECK( net.ServerNet.SendRpc( *serverPawn.Get(), 6, NetCore::RpcKind::ToOwner, down.data(), down.size() ) );
    CHECK( net.ServerNet.SendRpc( *serverCrate.Get(), 7, NetCore::RpcKind::ToAll, down.data(), down.size() ) );
    const std::vector<uint8_t> tooBig( 1100, 0xAB );
    CHECK_FALSE( net.ServerNet.SendRpc( *serverCrate.Get(), 8, NetCore::RpcKind::ToAll, tooBig.data(), tooBig.size() ) );

    net.Run( 6.0 );
    REQUIRE( server.Rpcs.size() == 1 );
    CHECK( server.Rpcs[0].Entity == kPawn );
    CHECK( server.Rpcs[0].Id == 3 );
    CHECK( server.Rpcs[0].Data == payload );
    REQUIRE( client.Rpcs.size() == 2 );
    CHECK( client.Rpcs[0].Entity == kPawn );
    CHECK( client.Rpcs[0].Id == 6 );
    CHECK( client.Rpcs[0].Data == down );
    CHECK( client.Rpcs[1].Entity == kCrate );
    CHECK( client.Rpcs[1].Id == 7 );

    // Replicated script state: the crate's, and the owner's own pawn's (flagged owned: its
    // [Predicted] part comes from OwnerState instead).
    CHECK( client.Replicated[kCrate] == 7 );
    CHECK( client.Replicated[kPawn] == 100 );
    CHECK( client.ReadAsOwned[kPawn] );
    CHECK_FALSE( client.ReadAsOwned[kCrate] );

    // The dash: predicted exactly, edges and all.
    NetPredictor& predictor = *net.ClientNet.GetPredictor();
    MESSAGE( "dash: " << predictor.GetMatchCount() << " ticks confirmed, " << predictor.GetCorrectionCount() << " corrections, "
        << predictor.GetInitialSyncCount() << " initial sync; counter " << server.Predicted[kPawn] );
    CHECK( server.Predicted[kPawn] > 300 );
    CHECK( predictor.GetMatchCount() > 250 );
    CHECK( predictor.GetCorrectionCount() == 0 );
    CHECK( predictor.GetInitialSyncCount() <= 1 );

    // A change only the server makes: one correction, and the client's scripts re-run every tick
    // since (with the recorded commands) to catch up.
    const int resimmedBefore = client.ResimulatedTicks;
    bonusTick = static_cast<int>( net.ServerNet.GetServer()->GetTick() ) + 5;
    net.Run( 3.0 );
    CHECK( bonus == 1 );
    CHECK( predictor.GetCorrectionCount() == 1 );
    CHECK( predictor.GetLastCorrectionTick() == static_cast<uint32_t>( bonusTick ) );
    CHECK( client.ResimulatedTicks > resimmedBefore );
    CHECK( client.Predicted[kPawn] > 1000 );

    // A late joiner learns who owns the pawn (its glue goes in before it connects: the schema
    // hash is part of the handshake).
    FakeScripts lateScripts;
    Peer& late = net.AddClient( Link( 30.f, 63 ), [&lateScripts]( World& InWorld ) {
        for( uint64_t guid : { kPawn, kCrate } )
        {
            EntityHandle entity = InWorld.CreateEntityWithGUID( guid );
            entity->AddComponent<Transform>();
            entity->AddComponent<NetIdentity>();
        }
        lateScripts.Net = InWorld.GetCore<NetCore>();
        lateScripts.Net->SetScriptGlue( lateScripts.Glue() );
    } );
    net.Run( 1.0 );
    REQUIRE( late.Net.GetClient()->GetState() == NetClient::State::Connected );
    CHECK( Find( *late.GameWorld, kPawn )->GetComponent<NetIdentity>().GetOwner() == 1 );
    CHECK_FALSE( late.Net.IsPredicted( *Find( *late.GameWorld, kPawn ).Get() ) );

    // Ownership moves away: the first client stops predicting and follows snapshots again.
    const uint8_t lateId = late.Net.GetClient()->GetClientId();
    net.ServerNet.SetOwner( *serverPawn.Get(), lateId );
    net.Run( 0.5 );
    CHECK( clientPawn->GetComponent<NetIdentity>().GetOwner() == lateId );
    CHECK_FALSE( net.ClientNet.IsPredicted( *clientPawn.Get() ) );
    CHECK( net.ClientNet.GetPredictor() == nullptr );
    CHECK_FALSE( net.ClientNet.SimulatesLocally( *clientPawn.Get() ) );

    // ... and when its new owner leaves, it goes back to the server (a later client given the
    // same id mustn't inherit it).
    late.Net.Close( net.Now() );
    net.Run( 0.5 );
    CHECK( net.ServerNet.GetOwner( *serverPawn.Get() ) == 0 );
    CHECK( clientPawn->GetComponent<NetIdentity>().GetOwner() == 0 );
}
