#include "PCH.h"
#include <doctest/doctest.h>
#include "Net/NetSession.h"
#include "Net/UdpTransport.h"
#include "NetHarness.h"
#include <chrono>
#include <thread>

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
