#include "PCH.h"
#include <doctest/doctest.h>
#include "Net/NetSession.h"
#include "Net/UdpTransport.h"
#include "NetHarness.h"
#include "Events/EventReceiver.h"
#include "JSON.h"
#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace NetHarness;

namespace NetUdpTest
{
    double Seconds()
    {
        return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    }

    // Polls until a packet arrives (real sockets: it takes a moment), up to InTimeout seconds.
    bool WaitFor( UdpTransport& InTransport, NetPacket& OutPacket, double InTimeout = 2.0 )
    {
        const double end = Seconds() + InTimeout;
        while( Seconds() < end )
        {
            if( InTransport.Receive( OutPacket ) )
            {
                return true;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return false;
    }

    NetConditions Clean( uint64_t InSeed )
    {
        NetConditions conditions;
        conditions.LatencyMs = 20.f;
        conditions.Seed = InSeed;
        return conditions;
    }

    // The NetSessionEvents one world's NetCore raises (every world in the process shares the
    // event bus), as a listener such as ScriptCore or a C++ game hears them.
    struct SessionLog
    {
        explicit SessionLog( const World& InWorld )
            : Subscription( EventSubscription::Create<NetSessionEvent>( [this, world = &InWorld]( const NetSessionEvent& InEvent ) {
                if( InEvent.Source == world )
                {
                    Events.push_back( InEvent );
                }
            } ) )
        {
        }
        SessionLog( const SessionLog& ) = delete;
        SessionLog& operator=( const SessionLog& ) = delete;

        std::vector<NetSessionEvent> Take() { return std::exchange( Events, {} ); }

        std::vector<NetSessionEvent> Events;
        std::unique_ptr<EventSubscription> Subscription;
    };
}

TEST_CASE( "Net: UDP addresses resolve, print and reject nonsense" )
{
    const NetAddress local = UdpTransport::Resolve( "127.0.0.1:7777" );
    CHECK( local == UdpTransport::MakeAddress( 0x7F000001u, 7777 ) );
    CHECK( UdpTransport::ToString( local ) == "127.0.0.1:7777" );
    // IPv4 only, so the name can't come back as ::1 (replies would never match).
    CHECK( UdpTransport::Resolve( "localhost:7777" ) == local );
    CHECK( UdpTransport::ToString( UdpTransport::MakeAddress( 0xC0A80A02u, 65535 ) ) == "192.168.10.2:65535" );
    for( const char* bad : { "", "127.0.0.1", ":7777", "127.0.0.1:", "127.0.0.1:0", "127.0.0.1:65536", "127.0.0.1:77a", "127.0.0.1:123456" } )
    {
        CAPTURE( bad );
        CHECK( UdpTransport::Resolve( bad ) == 0 );
    }
}

TEST_CASE( "Net: UDP transports trade datagrams on localhost, and a taken port fails to bind" )
{
    UdpTransport a( 0, "127.0.0.1" );
    UdpTransport b( 0, "127.0.0.1" );
    REQUIRE( a.IsOpen() );
    REQUIRE( b.IsOpen() );
    CHECK( a.GetError().empty() );
    CHECK( a.GetPort() != 0 );
    CHECK( a.GetAddress() == UdpTransport::Resolve( "127.0.0.1:" + std::to_string( a.GetPort() ) ) );

    NetPacket packet;
    CHECK_FALSE( b.Receive( packet ) );    // non-blocking: nothing yet

    const std::vector<uint8_t> hello = { 1, 2, 3, 250, 0, 7 };
    a.Send( b.GetAddress(), hello.data(), hello.size() );
    REQUIRE( NetUdpTest::WaitFor( b, packet ) );
    // The sender's address is exactly what it would resolve to: replies match.
    CHECK( packet.From == a.GetAddress() );
    CHECK( packet.Data == hello );

    std::vector<uint8_t> big( 1200 );
    for( size_t i = 0; i < big.size(); ++i )
    {
        big[i] = static_cast<uint8_t>( i * 7 );
    }
    b.Send( packet.From, big.data(), big.size() );
    REQUIRE( NetUdpTest::WaitFor( a, packet ) );
    CHECK( packet.From == b.GetAddress() );
    CHECK( packet.Data == big );
    CHECK( a.GetSendErrors() == 0 );
    CHECK( b.GetSendErrors() == 0 );

    UdpTransport taken( b.GetPort(), "127.0.0.1" );
    CHECK_FALSE( taken.IsOpen() );
    CHECK_FALSE( taken.GetError().empty() );
}

TEST_CASE( "Net: a client connects to a server over UDP and says goodbye" )
{
    UdpTransport serverSocket( 0, "127.0.0.1" );
    UdpTransport clientSocket( 0, "127.0.0.1" );
    REQUIRE( serverSocket.IsOpen() );
    REQUIRE( clientSocket.IsOpen() );
    NetVersion version;
    version.Build = 42;
    NetServer server( serverSocket, version, 60 );
    // Resolved from text, the way --connect does it.
    NetClient client( clientSocket, UdpTransport::Resolve( "localhost:" + std::to_string( serverSocket.GetPort() ) ), version );
    client.Connect();

    auto step = [&]() {
        const double now = NetUdpTest::Seconds();
        server.Receive( now );
        server.BeginTick();
        server.EndTick( now );
        client.Receive( now );
        client.SubmitCommand( InputCommand() );
        client.Flush( now );
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    };
    const double end = NetUdpTest::Seconds() + 3.0;
    while( client.GetState() != NetClient::State::Connected && NetUdpTest::Seconds() < end )
    {
        step();
    }
    REQUIRE( client.GetState() == NetClient::State::Connected );
    CHECK( server.GetClientCount() == 1 );
    CHECK( server.GetAcceptedCount() == 1 );
    CHECK( client.GetConnectedAt() > 0.0 );
    for( int i = 0; i < 20; ++i )
    {
        step();
    }
    CHECK( server.GetClients().front()->Commands.GetOnTimeCount() > 0 );

    client.Disconnect( NetUdpTest::Seconds() );
    CHECK( client.GetState() == NetClient::State::Disconnected );
    CHECK( client.GetDisconnectReason() == "left" );
    const double gone = NetUdpTest::Seconds() + 2.0;
    while( server.GetClientCount() != 0 && NetUdpTest::Seconds() < gone )
    {
        server.Receive( NetUdpTest::Seconds() );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    CHECK( server.GetClientCount() == 0 );
    CHECK( server.GetDisconnectCount() == 1 );
    CHECK( server.GetTimeoutCount() == 0 );
}

TEST_CASE( "Net: peers that go quiet time out, and goodbyes end sessions at once" )
{
    SUBCASE( "the server drops a silent client" )
    {
        Harness net( NetUdpTest::Clean( 1 ), NetUdpTest::Clean( 2 ) );
        net.ServerNet.SetTimeout( 1.0 );
        net.Start();
        net.Run( 1.0 );
        REQUIRE( net.ServerNet.GetServer()->GetClientCount() == 1 );
        int removed = 0;
        auto previous = net.ServerNet.GetServer()->OnClientRemoved;
        net.ServerNet.GetServer()->OnClientRemoved = [&]( NetServer::Client& InClient ) {
            ++removed;
            previous( InClient );
        };
        // The client stops: only the server keeps running.
        net.ClientFrameOffsetMs = 0;
        net.ClientNet.Shutdown();
        NetServer& server = *net.ServerNet.GetServer();
        for( int ms = 0; ms < 1500; ++ms )
        {
            const double now = ( ++net.Milliseconds ) / 1000.0;
            net.ServerLink.Update( now );
            net.ServerNet.Receive( now );
            net.ServerNet.BeginTick();
            net.ServerNet.EndTick( now );
        }
        CHECK( server.GetClientCount() == 0 );
        CHECK( server.GetTimeoutCount() == 1 );
        CHECK( server.GetDisconnectCount() == 0 );
        CHECK( removed == 1 );
    }
    SUBCASE( "a client gives up on a silent server, and on one that never answers" )
    {
        Harness net( NetUdpTest::Clean( 3 ), NetUdpTest::Clean( 4 ) );
        net.ClientNet.SetTimeout( 1.0 );
        net.Start();
        net.Run( 1.0 );
        NetClient& client = *net.ClientNet.GetClient();
        REQUIRE( client.GetState() == NetClient::State::Connected );
        // Whatever is already in flight lands now; then the server says nothing more.
        const double now = net.Now();
        client.Receive( now );
        client.Receive( now + 0.9 );
        CHECK( client.GetState() == NetClient::State::Connected );
        client.Receive( now + 1.1 );
        CHECK( client.GetState() == NetClient::State::TimedOut );

        LoopbackNetwork nowhere;
        LoopbackTransport alone( nowhere, 9 );
        NetClient lonely( alone, 10, NetVersion() );
        lonely.SetTimeout( 2.0 );
        lonely.Connect();
        lonely.Receive( 5.0 );
        lonely.Flush( 5.0 );
        lonely.Receive( 6.9 );
        CHECK( lonely.GetState() == NetClient::State::Connecting );
        lonely.Receive( 7.1 );
        CHECK( lonely.GetState() == NetClient::State::TimedOut );
    }
    SUBCASE( "a closing server tells its clients, a leaving client tells the server" )
    {
        Harness net( NetUdpTest::Clean( 5 ), NetUdpTest::Clean( 6 ) );
        net.Start();
        net.Run( 1.0 );
        REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
        net.ServerNet.Close( net.Now() );
        CHECK( net.ServerNet.GetServer()->GetClientCount() == 0 );
        // Close releases what the 20 ms link held: the client hears it on its next frame.
        net.ClientNet.Receive( net.Now() + 0.001 );
        CHECK( net.ClientNet.GetClient()->GetState() == NetClient::State::Disconnected );
        CHECK( net.ClientNet.GetClient()->GetDisconnectReason() == "server closed" );

        Harness leave( NetUdpTest::Clean( 7 ), NetUdpTest::Clean( 8 ) );
        leave.Start();
        leave.Run( 1.0 );
        REQUIRE( leave.ServerNet.GetServer()->GetClientCount() == 1 );
        leave.ClientNet.Close( leave.Now() );
        CHECK( leave.ClientNet.GetClient()->GetState() == NetClient::State::Disconnected );
        leave.ServerNet.Receive( leave.Now() + 0.001 );
        CHECK( leave.ServerNet.GetServer()->GetClientCount() == 0 );
        CHECK( leave.ServerNet.GetServer()->GetDisconnectCount() == 1 );
        CHECK( leave.ServerNet.GetServer()->GetTimeoutCount() == 0 );
    }
}

TEST_CASE( "Net: a client the server timed out hears about it instead of hanging half-open" )
{
    Harness net( NetUdpTest::Clean( 9 ), NetUdpTest::Clean( 10 ) );
    net.ServerNet.SetTimeout( 1.0 );
    net.Start();
    net.Run( 1.0 );
    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    // The client goes mute (its packets stop) but keeps listening.
    for( int ms = 0; ms < 1500; ++ms )
    {
        const double now = ( ++net.Milliseconds ) / 1000.0;
        net.ServerLink.Update( now );
        net.ServerNet.Receive( now );
        net.ServerNet.BeginTick();
        net.ServerNet.EndTick( now );
        client.Receive( now );
    }
    CHECK( net.ServerNet.GetServer()->GetTimeoutCount() == 1 );
    CHECK( client.GetState() == NetClient::State::Disconnected );
    CHECK( client.GetDisconnectReason() == "timed out" );
    CHECK( client.GetEndedAt() > client.GetConnectedAt() );
}

TEST_CASE( "Net: client ids stay unique and never 0 across hundreds of joins and leaves" )
{
    LoopbackNetwork network;
    LoopbackTransport serverSocket( network, 1 );
    NetServer server( serverSocket, NetVersion(), 60, 4 );
    // One client stays the whole time: nobody may be handed its id.
    std::vector<std::unique_ptr<LoopbackTransport>> sockets;
    std::vector<std::unique_ptr<NetClient>> clients;
    double now = 0.0;
    auto join = [&]( NetAddress InAddress ) -> NetClient& {
        sockets.push_back( std::make_unique<LoopbackTransport>( network, InAddress ) );
        clients.push_back( std::make_unique<NetClient>( *sockets.back(), 1, NetVersion() ) );
        NetClient& client = *clients.back();
        client.Connect();
        for( int step = 0; step < 4 && client.GetState() != NetClient::State::Connected; ++step )
        {
            now += 0.01;
            client.Flush( now );
            server.Receive( now );
            server.BeginTick();
            server.EndTick( now );
            client.Receive( now );
        }
        return client;
    };
    const uint8_t stayer = join( 2 ).GetClientId();
    REQUIRE( stayer != 0 );
    for( NetAddress address = 3; address < 303; ++address )
    {
        NetClient& client = join( address );
        REQUIRE( client.GetState() == NetClient::State::Connected );
        CHECK( client.GetClientId() != 0 );
        CHECK( client.GetClientId() != stayer );
        client.Disconnect( now );
        server.Receive( now );
        // Keep the one who stays heard from.
        clients.front()->Flush( now );
        server.Receive( now );
    }
    CHECK( server.GetClientCount() == 1 );
    CHECK( server.GetAcceptedCount() == 301 );
    CHECK( server.GetDisconnectCount() == 300 );
}

TEST_CASE( "Net: a client drawing far faster than the tick rate sends about one packet a tick" )
{
    // 1000 frames a second for 3 s against a 60 Hz server: its packets must not flood the link
    // (nor age out of the server's ack window and read as loss).
    Harness net( NetUdpTest::Clean( 11 ), NetUdpTest::Clean( 12 ) );
    net.ClientFrameSeconds = 0.001;
    net.Start();
    net.Run( 1.0 );
    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    const uint64_t sentBefore = client.GetConnection().GetStats().PacketsSent;
    for( int ms = 0; ms < 3000; ++ms )
    {
        const double now = ( ++net.Milliseconds ) / 1000.0;
        net.ServerLink.Update( now );
        net.ClientLink.Update( now );
        NetServer& server = *net.ServerNet.GetServer();
        while( server.GetTick() * ( 1.0 / 60.0 ) <= now - net.ServerStart )
        {
            net.ServerNet.Receive( now );
            net.ServerNet.BeginTick();
            net.ServerNet.EndTick( now );
        }
        net.ClientStep( net.ClientNet, now );
    }
    const NetStats& stats = client.GetConnection().GetStats();
    const double perSecond = static_cast<double>( stats.PacketsSent - sentBefore ) / 3.0;
    MESSAGE( "client packets/s at 1000 fps: " << perSecond << ", loss estimate " << stats.PacketLoss );
    CHECK( perSecond < 70.0 );
    CHECK( perSecond > 50.0 );
    CHECK( stats.PacketLoss < 0.05f );
    CHECK( client.GetState() == NetClient::State::Connected );
}


TEST_CASE( "Net: gameplay hears who joined and left and why, on the server and on each client" )
{
    using Kind = NetSessionEvent::Kind;
    using NetUdpTest::SessionLog;
    Harness net( NetUdpTest::Clean( 21 ), NetUdpTest::Clean( 22 ) );
    SessionLog serverLog( *net.ServerWorld );
    SessionLog clientLog( *net.ClientWorld );
    // Every listener hears every event: nobody takes them from the others (ScriptCore included).
    SessionLog serverGameplay( *net.ServerWorld );
    net.Start();
    net.Run( 1.0 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    const uint8_t first = net.ClientNet.GetClient()->GetClientId();
    std::vector<NetSessionEvent> server = serverLog.Take();
    REQUIRE( server.size() == 1 );
    CHECK( server[0].Type == Kind::ClientJoined );
    CHECK( server[0].ClientId == first );
    CHECK( server[0].Source == net.ServerWorld.get() );
    const std::vector<NetSessionEvent> gameplay = serverGameplay.Take();
    REQUIRE( gameplay.size() == 1 );
    CHECK( gameplay[0].Type == Kind::ClientJoined );
    CHECK( gameplay[0].ClientId == first );
    std::vector<NetSessionEvent> client = clientLog.Take();
    REQUIRE( client.size() == 1 );
    CHECK( client[0].Type == Kind::Connected );
    CHECK( client[0].ClientId == first );
    CHECK( client[0].Source == net.ClientWorld.get() );
    net.Run( 0.5 );
    CHECK( serverLog.Take().empty() );     // each event once
    CHECK( clientLog.Take().empty() );

    // A second client joins; the first says goodbye.
    Peer& second = net.AddClient( NetUdpTest::Clean( 23 ) );
    SessionLog secondLog( *second.GameWorld );
    net.Run( 1.0 );
    REQUIRE( second.Net.GetClient()->GetState() == NetClient::State::Connected );
    const uint8_t secondId = second.Net.GetClient()->GetClientId();
    CHECK( secondId != first );
    server = serverLog.Take();
    REQUIRE( server.size() == 1 );
    CHECK( server[0].Type == Kind::ClientJoined );
    CHECK( server[0].ClientId == secondId );
    CHECK( clientLog.Take().empty() );     // another world's session
    net.ClientNet.Close( net.Now() );
    client = clientLog.Take();
    REQUIRE( client.size() == 1 );
    CHECK( client[0].Type == Kind::Disconnected );
    CHECK( client[0].Reason == "left" );
    net.Run( 0.5 );
    server = serverLog.Take();
    REQUIRE( server.size() == 1 );
    CHECK( server[0].Type == Kind::ClientLeft );
    CHECK( server[0].ClientId == first );
    CHECK( server[0].Reason == "left" );

    // The server shuts down: its last client is "closed", and hears why.
    net.ServerNet.Close( net.Now() );
    server = serverLog.Take();
    REQUIRE( server.size() == 1 );
    CHECK( server[0].Type == Kind::ClientLeft );
    CHECK( server[0].ClientId == secondId );
    CHECK( server[0].Reason == "closed" );
    second.Net.Receive( net.Now() + 0.001 );
    client = secondLog.Take();
    REQUIRE( client.size() == 2 );
    CHECK( client[0].Type == Kind::Connected );
    CHECK( client[1].Type == Kind::Disconnected );
    CHECK( client[1].Reason == "server closed" );

    // A client that goes mute times out on the server; it hears the server's verdict.
    Harness mute( NetUdpTest::Clean( 24 ), NetUdpTest::Clean( 25 ) );
    SessionLog muteServer( *mute.ServerWorld );
    SessionLog muteClient( *mute.ClientWorld );
    mute.ServerNet.SetTimeout( 1.0 );
    mute.Start();
    mute.Run( 1.0 );
    REQUIRE( mute.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    muteServer.Take();
    muteClient.Take();
    for( int ms = 0; ms < 1500; ++ms )
    {
        const double now = ( ++mute.Milliseconds ) / 1000.0;
        mute.ServerLink.Update( now );
        mute.ServerNet.Receive( now );
        mute.ServerNet.BeginTick();
        mute.ServerNet.EndTick( now );
        mute.ClientNet.Receive( now );
    }
    server = muteServer.Take();
    REQUIRE( server.size() == 1 );
    CHECK( server[0].Type == Kind::ClientLeft );
    CHECK( server[0].Reason == "timed out" );
    client = muteClient.Take();
    REQUIRE( client.size() == 1 );
    CHECK( client[0].Type == Kind::Disconnected );
    CHECK( client[0].Reason == "timed out" );

    // Turned away: the session ends before it started.
    Harness rejected( NetUdpTest::Clean( 26 ), NetUdpTest::Clean( 27 ) );
    SessionLog rejectedServer( *rejected.ServerWorld );
    SessionLog rejectedClient( *rejected.ClientWorld );
    NetVersion other = rejected.ClientNet.MakeVersion();
    other.Build ^= 1;
    rejected.ClientNet.SetVersionOverride( other );
    rejected.Start();
    rejected.Run( 1.0 );
    CHECK( rejectedServer.Take().empty() );
    client = rejectedClient.Take();
    REQUIRE( client.size() == 1 );
    CHECK( client[0].Type == Kind::Disconnected );
    CHECK( client[0].Reason == "rejected: version mismatch: build" );

    // A server whose goodbye isn't UTF-8: the reason arrives as text (U+FFFD for each bad byte),
    // so it can't break the JSON that carries it to scripts.
    Harness rude( NetUdpTest::Clean( 28 ), NetUdpTest::Clean( 29 ) );
    SessionLog rudeClient( *rude.ClientWorld );
    rude.Start();
    rude.Run( 1.0 );
    REQUIRE( rude.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    rudeClient.Take();
    rude.ServerNet.GetServer()->Close( rude.Now(), "bye \xFF\xC0\xAF!" );
    rude.ServerLink.Update( rude.Now() + 1.0 );
    rude.ClientNet.Receive( rude.Now() + 1.0 );
    client = rudeClient.Take();
    REQUIRE( client.size() == 1 );
    CHECK( client[0].Type == Kind::Disconnected );
    CHECK( client[0].Reason == "bye \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD!" );
    CHECK( rude.ClientNet.GetClient()->GetDisconnectReason() == client[0].Reason );
    CHECK_NOTHROW( json( { { "Type", "NetDisconnected" }, { "Reason", client[0].Reason } } ).dump() );
}
