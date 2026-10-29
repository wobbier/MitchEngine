#pragma once
#include "Components/Networking/NetIdentity.h"
#include "Components/Transform.h"
#include "Cores/NetCore.h"
#include "Engine/World.h"
#include "Net/NetTransport.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// A server world and a client world in one process (NET-2), joined by conditioned loopbacks and
// stepped in simulated real time at 1 ms resolution. The server ticks at exactly its tick rate,
// the client renders frames at 60 Hz and runs however many ticks its dilated clock says are due,
// the way Engine::Run drives NetCore.
namespace NetHarness
{
    constexpr NetAddress kServer = 1;
    constexpr NetAddress kClient = 2;

    // A client beyond the first (late joiners): its own address, link, world and NetCore.
    struct Peer
    {
        LoopbackTransport Raw;
        ConditionedTransport Link;
        NetCore Net;    // before the world: it must outlive it
        std::shared_ptr<World> GameWorld = std::make_shared<World>();

        Peer( LoopbackNetwork& InNetwork, NetAddress InAddress, const NetConditions& InToServer )
            : Raw( InNetwork, InAddress )
            , Link( Raw, InToServer )
        {
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Net );
        }
    };

    struct Harness
    {
        LoopbackNetwork Network;
        LoopbackTransport ServerRaw{ Network, kServer };
        LoopbackTransport ClientRaw{ Network, kClient };
        ConditionedTransport ServerLink;   // server -> client
        ConditionedTransport ClientLink;   // client -> server
        // Cores before the worlds: they must outlive them.
        NetCore ServerNet;
        NetCore ClientNet;
        std::shared_ptr<World> ServerWorld = std::make_shared<World>();
        std::shared_ptr<World> ClientWorld = std::make_shared<World>();
        int64_t Milliseconds = 0;
        double ClientFrameSeconds = 1.0 / 60.0;
        int64_t ClientFrameOffsetMs = 7;    // the two loops aren't in phase
        // Called for each simulated tick.
        std::function<void( NetServer& )> OnServerTick;
        std::function<InputCommand( uint32_t InTick )> MakeCommand;
        std::function<void( NetClient& )> OnClientFrame;
        // The first client's simulation of each of its ticks (between BeginTick and EndTick).
        std::function<void( NetCore& )> OnClientTick;

        Harness( const NetConditions& InToClient, const NetConditions& InToServer )
            : ServerLink( ServerRaw, InToClient )
            , ClientLink( ClientRaw, InToServer )
        {
            ServerWorld->IsLoading = false;
            ClientWorld->IsLoading = false;
            ServerWorld->AddCore( ServerNet );
            ClientWorld->AddCore( ClientNet );
        }

        // The same placed networked entity in both worlds (the scene hash matches).
        void Place( uint64_t InGUID )
        {
            for( World* world : { ServerWorld.get(), ClientWorld.get() } )
            {
                EntityHandle entity = world->CreateEntityWithGUID( InGUID );
                entity->AddComponent<Transform>();
                entity->AddComponent<NetIdentity>();
            }
        }

        std::vector<std::unique_ptr<Peer>> ExtraClients;

        // Another client, connecting now (call Place-style setup on its world first if needed).
        Peer& AddClient( const NetConditions& InToServer, const std::function<void( World& )>& InSetup = {} )
        {
            ExtraClients.push_back( std::make_unique<Peer>( Network, kClient + ExtraClients.size() + 1, InToServer ) );
            Peer& peer = *ExtraClients.back();
            if( InSetup )
            {
                InSetup( *peer.GameWorld );
            }
            peer.Net.Connect( peer.Link, kServer );
            return peer;
        }

        void Start( const NetClockSettings& InClock = NetClockSettings() )
        {
            ServerNet.Host( ServerLink );
            ClientNet.Connect( ClientLink, kServer, InClock );
        }

        double Now() const { return Milliseconds / 1000.0; }
        int64_t ClientFrame( int64_t InMilliseconds ) const { return ( InMilliseconds + 1000 - ClientFrameOffsetMs ) * 60 / 1000; }

        void Run( double InSeconds )
        {
            const int64_t end = Milliseconds + static_cast<int64_t>( InSeconds * 1000.0 + 0.5 );
            while( Milliseconds < end )
            {
                ++Milliseconds;
                const double now = Now();
                ServerLink.Update( now );
                ClientLink.Update( now );
                for( auto& peer : ExtraClients )
                {
                    peer->Link.Update( now );
                }

                // Server ticks on its exact schedule (to the millisecond).
                NetServer* server = ServerNet.GetServer();
                const double tickSeconds = 1.0 / server->GetTickRate();
                while( server->GetTick() * tickSeconds <= now - ServerStart )
                {
                    ServerNet.Receive( now );
                    ServerNet.BeginTick();
                    if( OnServerTick )
                    {
                        OnServerTick( *server );
                    }
                    ServerNet.EndTick( now );
                }

                // Client frames at 60 Hz: due ticks, interpolated poses, then one packet.
                if( ClientFrame( Milliseconds ) != ClientFrame( Milliseconds - 1 ) )
                {
                    ClientStep( ClientNet, now );
                    if( OnClientFrame )
                    {
                        OnClientFrame( *ClientNet.GetClient() );
                    }
                    for( auto& peer : ExtraClients )
                    {
                        ClientStep( peer->Net, now );
                    }
                }
            }
        }

        void ClientStep( NetCore& InNet, double InNow )
        {
            NetClient* client = InNet.GetClient();
            InNet.Receive( InNow );
            if( client->GetState() == NetClient::State::Connected )
            {
                const int ticks = client->GetClock().Advance( ClientFrameSeconds );
                for( int i = 0; i < ticks; ++i )
                {
                    if( MakeCommand )
                    {
                        InNet.SetLocalCommand( MakeCommand( client->GetClock().GetTick() ) );
                    }
                    InNet.BeginTick();
                    if( OnClientTick && &InNet == &ClientNet )
                    {
                        OnClientTick( InNet );
                    }
                    InNet.EndTick( InNow );
                }
            }
            InNet.WriteInterpolated( ClientFrameSeconds );
            InNet.Flush( InNow );
        }

        double ServerStart = 0.0;
    };
}
