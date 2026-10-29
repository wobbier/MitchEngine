#pragma once
#include "Components/Transform.h"
#include "Cores/NetCore.h"
#include "Engine/World.h"
#include "Net/NetTransport.h"
#include <cstdint>
#include <functional>
#include <memory>

// A server world and a client world in one process (NET-2), joined by conditioned loopbacks and
// stepped in simulated real time at 1 ms resolution. The server ticks at exactly its tick rate,
// the client renders frames at 60 Hz and runs however many ticks its dilated clock says are due,
// the way Engine::Run drives NetCore.
namespace NetHarness
{
    constexpr NetAddress kServer = 1;
    constexpr NetAddress kClient = 2;

    struct Harness
    {
        LoopbackNetwork Network;
        LoopbackTransport ServerRaw{ Network, kServer };
        LoopbackTransport ClientRaw{ Network, kClient };
        ConditionedTransport ServerLink;   // server -> client
        ConditionedTransport ClientLink;   // client -> server
        std::shared_ptr<World> ServerWorld = std::make_shared<World>();
        std::shared_ptr<World> ClientWorld = std::make_shared<World>();
        NetCore ServerNet;
        NetCore ClientNet;
        int64_t Milliseconds = 0;
        double ClientFrameSeconds = 1.0 / 60.0;
        int64_t ClientFrameOffsetMs = 7;    // the two loops aren't in phase
        // Called for each simulated tick.
        std::function<void( NetServer& )> OnServerTick;
        std::function<InputCommand( uint32_t InTick )> MakeCommand;
        std::function<void( NetClient& )> OnClientFrame;

        Harness( const NetConditions& InToClient, const NetConditions& InToServer )
            : ServerLink( ServerRaw, InToClient )
            , ClientLink( ClientRaw, InToServer )
        {
            ServerWorld->IsLoading = false;
            ClientWorld->IsLoading = false;
            ServerWorld->AddCore( ServerNet );
            ClientWorld->AddCore( ClientNet );
        }

        // The same placed entity in both worlds (the scene hash matches).
        void Place( uint64_t InGUID )
        {
            ServerWorld->CreateEntityWithGUID( InGUID )->AddComponent<Transform>();
            ClientWorld->CreateEntityWithGUID( InGUID )->AddComponent<Transform>();
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

                // Client frames at 60 Hz: due ticks, then one packet.
                if( ClientFrame( Milliseconds ) != ClientFrame( Milliseconds - 1 ) )
                {
                    NetClient* client = ClientNet.GetClient();
                    ClientNet.Receive( now );
                    if( client->GetState() == NetClient::State::Connected )
                    {
                        const int ticks = client->GetClock().Advance( ClientFrameSeconds );
                        for( int i = 0; i < ticks; ++i )
                        {
                            if( MakeCommand )
                            {
                                ClientNet.SetLocalCommand( MakeCommand( client->GetClock().GetTick() ) );
                            }
                            ClientNet.BeginTick();
                            ClientNet.EndTick( now );
                        }
                    }
                    ClientNet.Flush( now );
                    if( OnClientFrame )
                    {
                        OnClientFrame( *client );
                    }
                }
            }
        }

        double ServerStart = 0.0;
    };
}
