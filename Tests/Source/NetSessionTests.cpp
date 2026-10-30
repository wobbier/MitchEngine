#include "PCH.h"
#include <doctest/doctest.h>
#include "Net/BitStream.h"
#include "Net/NetClock.h"
#include "Net/NetCommands.h"
#include "NetHarness.h"
#include <algorithm>
#include <cmath>

using namespace NetHarness;

namespace
{
    InputCommand CommandFor( uint32_t InTick )
    {
        InputCommand command;
        command.Buttons = ( InTick / 30 ) % 2 ? 0x5u : 0x1u;
        command.Axes[0] = std::sin( InTick * 0.05f );
        command.Axes[1] = InTick % 120 < 60 ? 1.f : -0.25f;
        return command;
    }

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

TEST_CASE( "Net: commands delta-code and round-trip, gaps and all" )
{
    std::vector<InputCommand> commands;
    for( uint32_t tick = 100; tick < 112; ++tick )
    {
        commands.push_back( CommandFor( tick ) );
        commands.back().Tick = tick == 111 ? 140 : tick;    // a resync's jump
        for( float& axis : commands.back().Axes )
        {
            axis = NetCommands::QuantizeAxis( axis );
        }
    }
    BitWriter writer;
    NetCommands::Write( writer, commands.data(), commands.size() );
    // Mostly-unchanged commands cost a few bits after the first.
    CHECK( writer.GetByteCount() < 60 );
    BitReader reader( writer.GetBytes() );
    std::vector<InputCommand> back;
    REQUIRE( NetCommands::Read( reader, back ) );
    REQUIRE( back.size() == commands.size() );
    for( size_t i = 0; i < commands.size(); ++i )
    {
        CHECK( back[i] == commands[i] );
    }

    // The server's buffer: in time, a repeat when missing, too-late copies ignored.
    NetCommandBuffer buffer;
    CHECK( buffer.Consume( 5 ).Buttons == 0 );     // nothing yet: idle, not a miss
    InputCommand a = CommandFor( 10 );
    a.Tick = 10;
    InputCommand c = CommandFor( 12 );
    c.Tick = 12;
    buffer.Receive( a, 10 );
    buffer.Receive( c, 10 );
    CHECK( buffer.Consume( 10 ) == a );
    const InputCommand repeated = buffer.Consume( 11 );
    CHECK( repeated.Tick == 11 );
    CHECK( repeated.Buttons == a.Buttons );
    CHECK( buffer.Consume( 12 ) == c );
    CHECK( buffer.GetOnTimeCount() == 2 );
    CHECK( buffer.GetMissedCount() == 1 );
    buffer.Receive( a, 13 );
    CHECK( buffer.GetBuffered() == 0 );
}

TEST_CASE( "Net: aim rides commands to 0.0055 degrees, wraps, clamps, and costs two bits while it holds (NET-15)" )
{
    // Quantization: yaw wraps into [0, 360), pitch clamps to [-90, 90], both within 0.01 degrees.
    for( float yaw : { 0.f, 12.345f, 179.99f, 359.997f, -30.f, 725.f } )
    {
        const float wrapped = yaw - 360.f * std::floor( yaw / 360.f );
        const float back = NetCommands::QuantizeYaw( yaw );
        CHECK( back >= 0.f );
        CHECK( back < 360.f );
        const float error = std::abs( back - wrapped );
        CHECK( std::min( error, 360.f - error ) < 0.01f );
    }
    CHECK( NetCommands::QuantizePitch( 37.5f ) == doctest::Approx( 37.5f ).epsilon( 0.0003 ) );
    CHECK( NetCommands::QuantizePitch( 120.f ) == doctest::Approx( 90.f ) );
    CHECK( NetCommands::QuantizePitch( -120.f ) == doctest::Approx( -90.f ) );

    // A turn, a hold, aim dropped and picked up again: all round-trip exactly (quantized).
    std::vector<InputCommand> commands;
    for( uint32_t tick = 200; tick < 216; ++tick )
    {
        InputCommand command;
        command.Tick = tick;
        for( float& axis : command.Axes )
        {
            axis = NetCommands::QuantizeAxis( axis );   // as the server will see it
        }
        command.HasAim = tick < 210 || tick > 212;
        if( command.HasAim )
        {
            const float turn = tick < 205 ? ( tick - 200 ) * 7.5f : 30.f;    // turns, then holds
            command.Yaw = NetCommands::QuantizeYaw( 350.f + turn );
            command.Pitch = NetCommands::QuantizePitch( -10.f + turn * 0.1f );
        }
        commands.push_back( command );
    }
    BitWriter writer;
    NetCommands::Write( writer, commands.data(), commands.size() );
    BitReader reader( writer.GetBytes() );
    std::vector<InputCommand> back;
    REQUIRE( NetCommands::Read( reader, back ) );
    REQUIRE( back.size() == commands.size() );
    for( size_t i = 0; i < commands.size(); ++i )
    {
        CAPTURE( i );
        CHECK( back[i].HasAim == commands[i].HasAim );
        CHECK( back[i].Yaw == commands[i].Yaw );
        CHECK( back[i].Pitch == commands[i].Pitch );
        CHECK( back[i] == commands[i] );
    }
    CHECK( back[5].Yaw == doctest::Approx( 20.f ).epsilon( 0.001 ) );     // 350 + 30 wrapped

    // A held aim costs two flag bits per command (no aim: one), plus its 32 bits once.
    std::vector<InputCommand> held( 16 );
    std::vector<InputCommand> none( 16 );
    for( uint32_t i = 0; i < 16; ++i )
    {
        held[i].Tick = none[i].Tick = 300 + i;
        held[i].HasAim = true;
        held[i].Yaw = NetCommands::QuantizeYaw( 90.f );
    }
    BitWriter heldWriter;
    BitWriter noneWriter;
    NetCommands::Write( heldWriter, held.data(), held.size() );
    NetCommands::Write( noneWriter, none.data(), none.size() );
    CHECK( heldWriter.GetBitCount() - noneWriter.GetBitCount() == 32 + 16 );
}


TEST_CASE( "Net: the client clock dilates towards the target slack and resyncs when far off" )
{
    NetClock clock;
    clock.Start( 100 );
    CHECK( clock.Advance( 1.0 ) == 60 );
    clock.OnSlackReport( 5.f );     // 2 ticks too far ahead: slow down
    CHECK( clock.GetDilation() < 0.f );
    CHECK( clock.Advance( 1.0 ) < 60 );
    clock.OnSlackReport( -10.f );   // way behind: never more than 5%... but this is a resync
    CHECK( clock.GetResyncCount() == 1 );
    CHECK( clock.GetTick() == 100 + 13 );
    clock.OnSlackReport( -10.f );   // the next report still describes the old clock: ignored
    CHECK( clock.GetResyncCount() == 1 );
    for( int i = 0; i < 40; ++i )
    {
        clock.TakeTick();
    }
    clock.OnSlackReport( 1.f );
    CHECK( clock.GetDilation() == doctest::Approx( 0.04f ) );
    clock.OnSlackReport( 0.f );
    CHECK( clock.GetDilation() == doctest::Approx( 0.05f ) );   // capped
}

TEST_CASE( "Net: at 100+-20 ms and 5% loss, 99.9% of commands arrive on time, slack holds within 1, dilation within 5%" )
{
    Harness net( Link( 50.f, 10.f, 0.05f, 3 ), Link( 50.f, 10.f, 0.05f, 4 ) );
    for( uint64_t guid = 1; guid <= 8; ++guid )
    {
        net.Place( 0xC0FFEE00 + guid );
    }
    net.MakeCommand = CommandFor;
    std::vector<InputCommand> simulated;
    net.OnServerTick = [&simulated]( NetServer& InServer ) {
        if( const NetServer::Client* client = InServer.FindClient( 1 ) )
        {
            if( client->Commands.HasReceived() )
            {
                simulated.push_back( client->Current );
            }
        }
    };
    float worstDilation = 0.f;
    net.OnClientFrame = [&worstDilation]( NetClient& InClient ) { worstDilation = std::max( worstDilation, std::abs( InClient.GetClock().GetDilation() ) ); };
    net.Start();
    net.Run( 3.0 );     // handshake and settling

    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
    REQUIRE( remote != nullptr );
    // The link's 100 ms, plus up to a frame each way waiting to be polled.
    const float rtt = client.GetConnection().GetStats().RoundTripMs;
    CHECK( rtt > 90.f );
    CHECK( rtt < 100.f + 2000.f / 60.f );

    const uint64_t onTimeBefore = remote->Commands.GetOnTimeCount();
    const uint64_t missedBefore = remote->Commands.GetMissedCount();
    const int resyncsBefore = client.GetClock().GetResyncCount();
    const size_t simulatedBefore = simulated.size();
    // The target adapts (NET-14): the slack is measured against wherever it is, once it has
    // held for 3 s (the clock needs a moment to steer to a new target).
    float worstSlackError = 0.f;
    float lastTarget = client.GetClock().GetTargetSlack();
    uint32_t heldSince = net.ServerNet.GetServer()->GetTick();
    net.OnServerTick = [&]( NetServer& InServer ) {
        const NetServer::Client* c = InServer.FindClient( 1 );
        simulated.push_back( c->Current );
        const float targetNow = client.GetClock().GetTargetSlack();
        if( targetNow != lastTarget )
        {
            lastTarget = targetNow;
            heldSince = InServer.GetTick();
        }
        if( InServer.GetTick() - heldSince > 180 )
        {
            worstSlackError = std::max( worstSlackError, std::abs( c->Commands.GetSlack() - targetNow ) );
        }
    };
    worstDilation = 0.f;
    net.Run( 60.0 );

    const uint64_t onTime = remote->Commands.GetOnTimeCount() - onTimeBefore;
    const uint64_t missed = remote->Commands.GetMissedCount() - missedBefore;
    MESSAGE( "on time " << onTime << ", missed " << missed << ", worst slack error " << worstSlackError << ", worst dilation " << worstDilation
        << ", target slack " << client.GetClock().GetTargetSlack() );
    CHECK( onTime + missed >= 3500 );
    CHECK( static_cast<double>( onTime ) / static_cast<double>( onTime + missed ) >= 0.999 );
    CHECK( worstSlackError <= 1.f );
    CHECK( worstDilation <= 0.05f + 1e-6f );
    CHECK( client.GetClock().GetResyncCount() == resyncsBefore );
    // What the server simulated is what the client meant, tick for tick (when it arrived).
    size_t matching = 0;
    for( size_t i = simulatedBefore; i < simulated.size(); ++i )
    {
        InputCommand expected = CommandFor( simulated[i].Tick );
        expected.Tick = simulated[i].Tick;
        expected.ViewTick = simulated[i].ViewTick;  // the client fills in what it was looking at
        for( float& axis : expected.Axes )
        {
            axis = NetCommands::QuantizeAxis( axis );
        }
        matching += simulated[i] == expected ? 1 : 0;
    }
    CHECK( matching + missed >= simulated.size() - simulatedBefore );

    // The link gets 60 ms slower each way: the clock speeds up and the slack comes back.
    net.ServerLink.SetConditions( Link( 80.f, 10.f, 0.05f, 5 ) );
    net.ClientLink.SetConditions( Link( 80.f, 10.f, 0.05f, 6 ) );
    worstSlackError = 0.f;
    net.OnServerTick = nullptr;
    net.Run( 10.0 );
    CHECK( std::abs( remote->Commands.GetSlack() - client.GetClock().GetTargetSlack() ) <= 1.f );
    CHECK( std::abs( client.GetClock().GetDilation() ) < 0.05f );
}

TEST_CASE( "Net: the slack target grows with reported misses and eases back to a jitter floor when clean (NET-14)" )
{
    NetClockSettings settings;
    NetClock clock( settings );
    clock.Start( 0 );
    CHECK( clock.GetTargetSlack() == doctest::Approx( 3.f ) );
    clock.OnMisses( 2 );
    CHECK( clock.GetTargetSlack() == doctest::Approx( 5.f ) );
    clock.OnMisses( 40 );
    CHECK( clock.GetTargetSlack() == doctest::Approx( settings.MaxSlack ) );
    // 3 s clean, then a quarter tick a second, down to the floor (no jitter: MinSlack).
    for( int i = 0; i < 60 * 3; ++i )
    {
        clock.TakeTick();
    }
    CHECK( clock.GetTargetSlack() == doctest::Approx( settings.MaxSlack ) );
    for( int i = 0; i < 60 * 4; ++i )
    {
        clock.TakeTick();
    }
    CHECK( clock.GetTargetSlack() == doctest::Approx( settings.MaxSlack - 1.f ) );
    for( int i = 0; i < 60 * 60; ++i )
    {
        clock.TakeTick();
    }
    CHECK( clock.GetTargetSlack() == doctest::Approx( settings.MinSlack ) );
    // 20 ms of jitter is 1.2 ticks: the floor is 3 (twice it, rounded up).
    clock.SetJitter( 0.020 );
    CHECK( clock.GetTargetSlack() == doctest::Approx( 3.f ) );
    // A miss resets the clean run.
    clock.OnMisses( 1 );
    for( int i = 0; i < 60 * 3; ++i )
    {
        clock.TakeTick();
    }
    CHECK( clock.GetTargetSlack() == doctest::Approx( 4.f ) );

    NetClockSettings fixed;
    fixed.AdaptiveSlack = false;
    NetClock still( fixed );
    still.OnMisses( 5 );
    CHECK( still.GetTargetSlack() == doctest::Approx( 3.f ) );
}

TEST_CASE( "Net: a press in a command that arrives after its tick ran on a repeat still reaches the server, once (NET-14)" )
{
    NetCommandBuffer buffer;
    auto command = []( uint32_t InTick, uint32_t InButtons ) {
        InputCommand c;
        c.Tick = InTick;
        c.Buttons = InButtons;
        return c;
    };
    buffer.Receive( command( 10, 0 ), 10 );
    CHECK( buffer.Consume( 10 ).Buttons == 0 );
    // Tick 11's command (a one-tick tap of bit 2) is late: the server repeats tick 10's.
    CHECK( buffer.Consume( 11 ).Buttons == 0 );
    CHECK( buffer.GetMissedCount() == 1 );
    buffer.Receive( command( 11, 2u ), 12 );
    buffer.Receive( command( 11, 2u ), 12 );   // a redundant copy doesn't count twice
    buffer.Receive( command( 12, 0 ), 12 );
    CHECK( buffer.GetRecoveredPresses() == 1 );
    // Tick 12 gets the press merged in, for that tick only.
    CHECK( buffer.Consume( 12 ).Buttons == 2u );
    CHECK( buffer.Consume( 13 ).Buttons == 0 );     // missing again: the repeat doesn't carry it
    // A held button the repeat already had isn't a new press.
    NetCommandBuffer held;
    held.Receive( command( 20, 4u ), 20 );
    held.Consume( 20 );
    held.Consume( 21 );
    held.Receive( command( 21, 4u ), 22 );
    CHECK( held.GetRecoveredPresses() == 0 );
}

TEST_CASE( "Net: on a clean link the adaptive buffer settles at one to one and a half ticks (NET-14)" )
{
    Harness net( Link( 10.f, 0.f, 0.f, 7 ), Link( 10.f, 0.f, 0.f, 8 ) );
    net.MakeCommand = CommandFor;
    net.Start();
    net.Run( 40.0 );
    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
    REQUIRE( remote != nullptr );
    MESSAGE( "clean link: target " << client.GetClock().GetTargetSlack() << ", slack " << remote->Commands.GetSlack() << ", missed " << remote->Commands.GetMissedCount() );
    CHECK( client.GetClock().GetTargetSlack() <= 1.5f );
    CHECK( remote->Commands.GetSlack() >= 0.5f );
    CHECK( remote->Commands.GetSlack() <= 2.f );
}

TEST_CASE( "Net: a client with another build or scene is rejected and told why" )
{
    Harness net( Link( 20.f, 0.f, 0.f, 1 ), Link( 20.f, 0.f, 0.f, 2 ) );
    net.Place( 0xAB );
    NetVersion other = net.ClientNet.MakeVersion();
    CHECK( other == net.ServerNet.MakeVersion() );
    other.Build ^= 1;
    net.ClientNet.SetVersionOverride( other );
    net.Start();
    net.Run( 1.0 );
    NetClient& client = *net.ClientNet.GetClient();
    CHECK( client.GetState() == NetClient::State::Rejected );
    CHECK( client.GetRejectReason() == "version mismatch: build" );
    CHECK( net.ServerNet.GetServer()->GetClientCount() == 0 );
    net.Run( 3.0 );
    CHECK( net.ServerNet.GetServer()->GetClients().empty() );

    // A different set of placed entities.
    Harness scene( Link( 20.f, 0.f, 0.f, 1 ), Link( 20.f, 0.f, 0.f, 2 ) );
    scene.Place( 0xAB );
    EntityHandle extra = scene.ClientWorld->CreateEntityWithGUID( 0xCD );
    extra->AddComponent<Transform>();
    extra->AddComponent<NetIdentity>();
    scene.Start();
    scene.Run( 1.0 );
    CHECK( scene.ClientNet.GetClient()->GetState() == NetClient::State::Rejected );
    CHECK( scene.ClientNet.GetClient()->GetRejectReason() == "version mismatch: scene" );

    // A matching one gets in.
    Harness match( Link( 20.f, 0.f, 0.f, 1 ), Link( 20.f, 0.f, 0.f, 2 ) );
    match.Place( 0xAB );
    match.Start();
    match.Run( 1.0 );
    CHECK( match.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    CHECK( match.ServerNet.GetServer()->GetClientCount() == 1 );
}


TEST_CASE( "Net: a client with other net-required mods is rejected with the list; the same mods get in (MOD-3)" )
{
    Harness net( Link( 20.f, 0.f, 0.f, 1 ), Link( 20.f, 0.f, 0.f, 2 ) );
    net.Place( 0xAB );
    NetVersion server = net.ServerNet.MakeVersion();
    server.SetMods( { "com.example.core@1.2.0", "com.example.weapons@2.0.0" } );
    net.ServerNet.SetVersionOverride( server );
    NetVersion client = net.ClientNet.MakeVersion();
    client.SetMods( { "com.example.weapons@1.9.0", "com.example.core@1.2.0", "com.example.hats@1.0.0" } );
    net.ClientNet.SetVersionOverride( client );
    net.Start();
    net.Run( 1.0 );
    CHECK( net.ClientNet.GetClient()->GetState() == NetClient::State::Rejected );
    CHECK( net.ClientNet.GetClient()->GetRejectReason()
        == "version mismatch: mods (the server needs com.example.weapons@2.0.0; you also have com.example.hats@1.0.0, com.example.weapons@1.9.0)" );

    // The same mods, listed in another order: accepted.
    Harness same( Link( 20.f, 0.f, 0.f, 1 ), Link( 20.f, 0.f, 0.f, 2 ) );
    same.Place( 0xAB );
    NetVersion serverVersion = same.ServerNet.MakeVersion();
    serverVersion.SetMods( { "b@1.0.0", "a@1.0.0" } );
    same.ServerNet.SetVersionOverride( serverVersion );
    NetVersion clientVersion = same.ClientNet.MakeVersion();
    clientVersion.SetMods( { "a@1.0.0", "b@1.0.0" } );
    same.ClientNet.SetVersionOverride( clientVersion );
    CHECK( serverVersion.ModSet == clientVersion.ModSet );
    CHECK( serverVersion.ModSet != 0 );
    same.Start();
    same.Run( 1.0 );
    CHECK( same.ClientNet.GetClient()->GetState() == NetClient::State::Connected );

    // No net-required mods: ModSet 0, as before.
    NetVersion none;
    none.SetMods( {} );
    CHECK( none.ModSet == 0 );
}
