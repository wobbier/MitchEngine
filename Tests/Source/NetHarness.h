#pragma once
#include "Components/Networking/NetIdentity.h"
#include "Components/Transform.h"
#include "Cores/NetCore.h"
#include "Engine/World.h"
#include "Net/NetTransport.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// A server world and a client world in one process (NET-2), joined by conditioned loopbacks and
// stepped in simulated real time at 1 ms resolution. The server ticks at exactly its tick rate,
// the client renders frames at ClientFrameRate (60 Hz unless set) and runs however many ticks its
// dilated clock says are due, the way Engine::Run drives NetCore.
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
        int64_t ClientFrameRate = 60;       // frames a second (set ClientFrameSeconds to match)
        int64_t ClientFrameOffsetMs = 7;    // the two loops aren't in phase
        // Called for each simulated tick.
        std::function<void( NetServer& )> OnServerTick;
        std::function<InputCommand( uint32_t InTick )> MakeCommand;
        std::function<void( NetClient& )> OnClientFrame;
        // The first client's simulation of each of its ticks (between BeginTick and EndTick).
        std::function<void( NetCore& )> OnClientTick;
        // A hitch: the first client runs no frames until then (seconds; it neither receives nor
        // sends). On its next frame its clock catches up on at most ClientCatchUpTicks of the time
        // it missed, the way the engine runs a few steps a frame and drops the rest, so it can
        // come back behind.
        double ClientHitchUntil = -1.0;
        int ClientCatchUpTicks = 8;
        // A server hitch: no ticks (nor reads) until then (seconds). Then it catches up on the
        // ticks it owes (KeepsBacklog), each taking ServerCatchUpMs of wall time, as a Debug
        // server's do, so its reports for them go out spread over a few client frames. As in
        // Engine::Run, it keeps at most NetServer::kMaxCatchUpSeconds of them (the rest are lost),
        // and none when its ticks take longer than they cover (it can't keep up: ServerCatchUpMs
        // over a tick is a server that slow all the time).
        double ServerHitchUntil = -1.0;
        int64_t ServerCatchUpMs = 0;
        // Catching up, at most this many ticks a frame, a frame a tick (0: no limit, every tick due
        // at once). Engine::Run runs 8 fixed steps a frame and a headless server draws a frame a
        // tick, so its reports for the ticks owed go out in bursts of 8, a tick apart.
        int ServerStepsPerFrame = 0;
        // Every tick takes this long (a server that slow all the time; over a tick, it can't keep up).
        int64_t ServerTickMs = 0;
        // Called at the start of each simulated millisecond (change link conditions mid-run).
        std::function<void( double InNow )> OnMillisecond;

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

        void Start( const NetClockSettings& InClock = NetClockSettings(), uint16_t InTickRate = 60 )
        {
            ServerNet.Host( ServerLink, InTickRate );
            ClientNet.Connect( ClientLink, kServer, InClock );
        }

        double Now() const { return Milliseconds / 1000.0; }
        int64_t ClientFrame( int64_t InMilliseconds ) const { return ( InMilliseconds + 1000 - ClientFrameOffsetMs ) * ClientFrameRate / 1000; }

        void Run( double InSeconds )
        {
            const int64_t end = Milliseconds + static_cast<int64_t>( InSeconds * 1000.0 + 0.5 );
            while( Milliseconds < end )
            {
                ++Milliseconds;
                const double now = Now();
                if( OnMillisecond )
                {
                    OnMillisecond( now );
                }
                ServerLink.Update( now );
                ClientLink.Update( now );
                for( auto& peer : ExtraClients )
                {
                    peer->Link.Update( now );
                }

                // Server ticks on its exact schedule (to the millisecond).
                NetServer* server = ServerNet.GetServer();
                const double tickSeconds = 1.0 / server->GetTickRate();
                if( now >= ServerHitchUntil && Milliseconds >= m_serverBusyUntil )
                {
                    // The engine keeps a server's backlog to kMaxCatchUpSeconds: past it, ticks are lost.
                    const int64_t due = static_cast<int64_t>( std::floor( ( now - ServerStart ) / tickSeconds ) ) + 1 - static_cast<int64_t>( server->GetTick() );
                    const int64_t keep = static_cast<int64_t>( server->GetMaxTicksOwed() );
                    if( due > keep )
                    {
                        ServerStart += static_cast<double>( due - keep ) * tickSeconds;
                        ServerSecondsLost += static_cast<double>( due - keep ) * tickSeconds;
                    }
                }
                while( now >= ServerHitchUntil && Milliseconds >= m_serverBusyUntil && server->GetTick() * tickSeconds <= now - ServerStart )
                {
                    if( m_frameSteps == 0 )
                    {
                        m_frameStartMs = Milliseconds;
                    }
                    ServerNet.Receive( now );
                    // The ticks due after this one (a catch-up after a hitch).
                    const int64_t owed = static_cast<int64_t>( std::floor( ( now - ServerStart ) / tickSeconds ) ) - static_cast<int64_t>( server->GetTick() );
                    ServerNet.BeginTick( static_cast<uint32_t>( std::max<int64_t>( 0, owed ) ) );
                    if( OnServerTick )
                    {
                        OnServerTick( *server );
                    }
                    ServerNet.EndTick( now );
                    const bool behind = server->GetTick() * tickSeconds <= now - ServerStart;
                    const int64_t cost = std::max( ServerTickMs, behind ? ServerCatchUpMs : int64_t( 0 ) );
                    if( cost > 0 )
                    {
                        m_serverBusyUntil = Milliseconds + cost;
                        if( static_cast<double>( cost ) / 1000.0 > tickSeconds )
                        {
                            // Can't keep up: the backlog goes, the next tick runs once this one is done.
                            const double nextAt = static_cast<double>( m_serverBusyUntil ) / 1000.0;
                            const double start = nextAt - static_cast<double>( server->GetTick() ) * tickSeconds;
                            ServerSecondsLost += std::max( 0.0, start - ServerStart );
                            ServerStart = std::max( ServerStart, start );
                        }
                    }
                    if( !behind )
                    {
                        m_frameSteps = 0;
                    }
                    else if( ServerStepsPerFrame > 0 && ++m_frameSteps >= ServerStepsPerFrame )
                    {
                        // The frame's steps are done: the rest wait for the next frame, a tick on.
                        m_serverBusyUntil = std::max( m_serverBusyUntil, m_frameStartMs + static_cast<int64_t>( std::lround( tickSeconds * 1000.0 ) ) );
                        m_frameSteps = 0;
                    }
                }

                // Client frames (60 Hz unless set): due ticks, interpolated poses, then one packet.
                if( ClientFrame( Milliseconds ) != ClientFrame( Milliseconds - 1 ) )
                {
                    if( now < ClientHitchUntil )
                    {
                        m_hitchFrom = m_hitchFrom < 0.0 ? m_lastClientStep : m_hitchFrom;
                    }
                    else
                    {
                        double seconds = ClientFrameSeconds;
                        if( m_hitchFrom >= 0.0 )
                        {
                            const double clientTick = ClientNet.GetClient()->GetClock().GetSettings().TickSeconds;
                            seconds = std::min( now - m_hitchFrom, ClientFrameSeconds + ClientCatchUpTicks * clientTick );
                            m_hitchFrom = -1.0;
                        }
                        ClientStep( ClientNet, now, seconds );
                        m_lastClientStep = now;
                        if( OnClientFrame )
                        {
                            OnClientFrame( *ClientNet.GetClient() );
                        }
                    }
                    for( auto& peer : ExtraClients )
                    {
                        ClientStep( peer->Net, now );
                    }
                }
            }
        }

        void ClientStep( NetCore& InNet, double InNow, double InSeconds = -1.0 )
        {
            NetClient* client = InNet.GetClient();
            InNet.Receive( InNow );
            if( client->GetState() == NetClient::State::Connected )
            {
                const int ticks = client->GetClock().Advance( InSeconds >= 0.0 ? InSeconds : ClientFrameSeconds );
                for( int i = 0; i < ticks; ++i )
                {
                    if( MakeCommand )
                    {
                        Making = &InNet;
                        InNet.SetLocalCommand( MakeCommand( client->GetClock().GetTick() ) );
                        Making = nullptr;
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
        // Time the server's schedule lost: past kMaxCatchUpSeconds of a hitch, or all its backlog
        // when it can't keep up.
        double ServerSecondsLost = 0.0;
        uint64_t GetServerTicksLost() { return static_cast<uint64_t>( std::lround( ServerSecondsLost * ServerNet.GetServer()->GetTickRate() ) ); }
        // Whose command MakeCommand is making (ClientNet or a Peer's Net).
        const NetCore* Making = nullptr;

    private:
        double m_lastClientStep = 0.0;
        double m_hitchFrom = -1.0;
        int64_t m_serverBusyUntil = 0;
        int m_frameSteps = 0;
        int64_t m_frameStartMs = 0;
    };
}
