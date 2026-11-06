#include "PCH.h"
#include <doctest/doctest.h>
#include "Net/BitStream.h"
#include "Net/NetClock.h"
#include "Net/NetCommands.h"
#include "Net/NetQuantize.h"
#include "NetHarness.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string>

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

    // Watches a two-world run from the server's side, tick by tick: the slack against the client's
    // target, the misses, and how old each command the server ran from its buffer was. The client
    // holds move and fire; each command it makes carries its generation (a counter, in axes 2 and
    // 3), so a command made for a tick the client later made again (a resync back) still tells.
    struct ClockProbe
    {
        struct Sample
        {
            double At = 0.0;
            float Slack = 0.f;
            float Target = 0.f;
            uint64_t Missed = 0;
            double StaleMs = -1.0;  // < 0: a repeat
            uint32_t Tick = 0;
            uint32_t Generation = 0;
            bool FromBuffer = false;
        };
        std::vector<double> MadeAt;     // by generation
        std::vector<uint32_t> MadeTick; // by generation (~0u: another client's)
        std::map<uint32_t, uint32_t> Latest;    // tick -> the generation the client made for it last
        std::vector<Sample> Samples;

        void Attach( Harness& InNet )
        {
            InNet.MakeCommand = [this, &InNet]( uint32_t InTick ) {
                InputCommand c;
                c.Buttons = 0x5u;
                c.Axes[1] = 1.f;
                const uint32_t generation = static_cast<uint32_t>( MadeAt.size() );
                const bool mine = InNet.Making == &InNet.ClientNet;
                MadeAt.push_back( InNet.Now() );
                MadeTick.push_back( mine ? InTick : ~0u );
                if( mine )
                {
                    Latest[InTick] = generation;
                }
                c.Axes[2] = NetQuantize::Dequantize( generation & 0xFFFFu, -1.f, 1.f, 16 );
                c.Axes[3] = NetQuantize::Dequantize( generation >> 16, -1.f, 1.f, 16 );
                return c;
            };
            InNet.OnServerTick = [this, &InNet]( NetServer& InServer ) {
                const NetClient* client = InNet.ClientNet.GetClient();
                const NetServer::Client* c = client && client->GetClientId() != 0 ? InServer.FindClient( client->GetClientId() ) : nullptr;
                if( !c || !client || !c->Commands.HasReceived() )
                {
                    return;
                }
                Sample sample;
                sample.At = InNet.Now();
                sample.Slack = c->Commands.GetSlack();
                sample.Target = client->GetClock().GetTargetSlack();
                sample.Missed = c->Commands.GetMissedCount();
                sample.Tick = InServer.GetTick();
                if( c->Commands.GetRepeatRun() == 0 )
                {
                    const uint32_t generation = NetQuantize::Quantize( c->Current.Axes[2], -1.f, 1.f, 16 )
                        | ( NetQuantize::Quantize( c->Current.Axes[3], -1.f, 1.f, 16 ) << 16 );
                    if( generation < MadeAt.size() )
                    {
                        sample.StaleMs = ( sample.At - MadeAt[generation] ) * 1000.0;
                        sample.Generation = generation;
                        sample.FromBuffer = true;
                    }
                }
                Samples.push_back( sample );
            };
        }

        // Seconds from InFrom until the slack is within 1 tick of the target for good.
        double SettledAfter( double InFrom ) const
        {
            double last = InFrom;
            for( const Sample& sample : Samples )
            {
                if( sample.At >= InFrom && std::abs( sample.Slack - sample.Target ) > 1.f )
                {
                    last = sample.At;
                }
            }
            return last - InFrom;
        }

        // The server's misses on ticks in [InFrom, InTo).
        uint64_t Misses( double InFrom, double InTo ) const
        {
            uint64_t before = 0;
            uint64_t until = 0;
            for( const Sample& sample : Samples )
            {
                before = sample.At < InFrom ? sample.Missed : before;
                until = sample.At < InTo ? sample.Missed : until;
            }
            return until - std::min( until, before );
        }

        // The oldest command the server ran from its buffer in [InFrom, InTo), in ms.
        double WorstStaleMs( double InFrom, double InTo ) const
        {
            double worst = 0.0;
            for( const Sample& sample : Samples )
            {
                if( sample.At >= InFrom && sample.At < InTo )
                {
                    worst = std::max( worst, sample.StaleMs );
                }
            }
            return worst;
        }

        // The deepest the client's target got in [InFrom, InTo).
        float MaxTarget( double InFrom, double InTo ) const
        {
            float deepest = 0.f;
            for( const Sample& sample : Samples )
            {
                if( sample.At >= InFrom && sample.At < InTo )
                {
                    deepest = std::max( deepest, sample.Target );
                }
            }
            return deepest;
        }

        // Ticks the server ran on a command the client had since made again (a resync back): it
        // ran something the client took back.
        uint64_t Superseded() const
        {
            uint64_t count = 0;
            for( const Sample& sample : Samples )
            {
                if( !sample.FromBuffer || MadeTick[sample.Generation] != sample.Tick )
                {
                    continue;   // a repeat, or a resync forward's filler
                }
                auto latest = Latest.find( sample.Tick );
                count += latest != Latest.end() && latest->second != sample.Generation ? 1 : 0;
            }
            return count;
        }
    };

    // A link outage: InSeconds cut up (client to server), down, or both, after 12 s on InLink.
    struct OutageCase
    {
        double Seconds = 0.3;
        bool Up = true;
        bool Down = false;
        int64_t Fps = 60;           // the client's frame rate
        uint16_t TickRate = 60;     // the server's
        bool WithPeer = false;      // a second client on its own (clean) uplink throughout
    };

    // What a link outage does to the command clock.
    struct OutageReport
    {
        int Resyncs = 0;                // from the cut to the end
        double SettleSeconds = 0.0;     // from the link's return until the slack is within 1 tick of the target for good
        uint64_t OwnMisses = 0;         // repeats until the first commands sent after the return could arrive
        uint64_t ExtraMisses = 0;       // every repeat after that (a down-only cut has no own misses)
        double WorstStaleMs = 0.0;      // the oldest command the server ran from its buffer, from the cut on
        double BaselineStaleMs = 0.0;   // ... in the 5 s before the cut
        float TargetBefore = 0.f;
        float TargetAfter = 0.f;
        float MaxTarget = 0.f;          // the deepest the target got from the cut to the end
        float MaxTargetFirstSecond = 0.f;   // ... from the cut to a second after the first commands back could arrive
        uint64_t BaselineMisses = 0;    // in the 10 s before the cut
        uint64_t ExtraMissesFirstSecond = 0;
        uint64_t Stalls = 0;
        uint64_t Outages = 0;
        uint64_t Hitches = 0;           // the client's own (ME-313): none, the link went
        bool Connected = false;
        // The second client, from the cut to the end.
        int PeerResyncs = 0;
        uint64_t PeerStalls = 0;
        uint64_t PeerMisses = 0;
        uint64_t PeerBaselineMisses = 0;    // in the 10 s before the cut
        float PeerTargetBefore = 0.f;
        float PeerMaxTarget = 0.f;
    };

    OutageReport MeasureOutage( const OutageCase& InCase, const NetConditions& InLink, uint64_t InSeed )
    {
        NetConditions toClient = InLink;
        NetConditions toServer = InLink;
        toClient.Seed = InSeed;
        toServer.Seed = InSeed + 1;
        Harness net( toClient, toServer );
        net.ClientFrameRate = InCase.Fps;
        net.ClientFrameSeconds = 1.0 / static_cast<double>( InCase.Fps );
        ClockProbe probe;
        probe.Attach( net );
        NetClockSettings clock;
        clock.TickSeconds = 1.0 / InCase.TickRate;
        net.Start( clock, InCase.TickRate );
        Peer* peer = nullptr;
        if( InCase.WithPeer )
        {
            NetConditions peerUp = InLink;
            peerUp.Seed = InSeed + 6;
            peer = &net.AddClient( peerUp );
        }
        // Long enough for the target to ease down to where the link keeps it.
        net.Run( 2.0 );
        const NetServer::Client* peerAt10 = peer && peer->Net.GetClient()->GetClientId() != 0 ? net.ServerNet.GetServer()->FindClient( peer->Net.GetClient()->GetClientId() ) : nullptr;
        const uint64_t peerMissesAt10 = peerAt10 ? peerAt10->Commands.GetMissedCount() : 0;
        net.Run( 10.0 );
        OutageReport report;
        NetClient& client = *net.ClientNet.GetClient();
        if( client.GetState() != NetClient::State::Connected )
        {
            return report;
        }
        const double cutAt = net.Now();
        const int resyncsBefore = client.GetClock().GetResyncCount();
        const uint64_t stallsBefore = client.GetStallCount();
        const uint64_t outagesBefore = client.GetOutageCount();
        report.TargetBefore = client.GetClock().GetTargetSlack();
        NetClient* peerClient = peer ? peer->Net.GetClient() : nullptr;
        const NetServer::Client* peerRemote = peerClient ? net.ServerNet.GetServer()->FindClient( peerClient->GetClientId() ) : nullptr;
        const int peerResyncsBefore = peerClient ? peerClient->GetClock().GetResyncCount() : 0;
        const uint64_t peerStallsBefore = peerClient ? peerClient->GetStallCount() : 0;
        const uint64_t peerMissesBefore = peerRemote ? peerRemote->Commands.GetMissedCount() : 0;
        report.PeerTargetBefore = peerClient ? peerClient->GetClock().GetTargetSlack() : 0.f;
        report.PeerBaselineMisses = peerRemote ? peerRemote->Commands.GetMissedCount() - peerMissesAt10 : 0;
        if( peerClient )
        {
            const auto probeTick = net.OnServerTick;
            net.OnServerTick = [probeTick, peerClient, &report]( NetServer& InServer ) {
                probeTick( InServer );
                report.PeerMaxTarget = std::max( report.PeerMaxTarget, peerClient->GetClock().GetTargetSlack() );
            };
        }
        NetConditions cut = InLink;
        cut.Loss = 1.f;
        if( InCase.Down )
        {
            cut.Seed = InSeed + 2;
            net.ServerLink.SetConditions( cut );
        }
        if( InCase.Up )
        {
            cut.Seed = InSeed + 3;
            net.ClientLink.SetConditions( cut );
        }
        net.Run( InCase.Seconds );
        const double returnAt = net.Now();
        toClient.Seed = InSeed + 4;
        toServer.Seed = InSeed + 5;
        net.ServerLink.SetConditions( toClient );
        net.ClientLink.SetConditions( toServer );
        net.Run( 10.0 );

        report.Connected = client.GetState() == NetClient::State::Connected;
        report.Resyncs = client.GetClock().GetResyncCount() - resyncsBefore;
        report.Stalls = client.GetStallCount() - stallsBefore;
        report.Outages = client.GetOutageCount() - outagesBefore;
        report.Hitches = client.GetHitchCount();
        report.TargetAfter = client.GetClock().GetTargetSlack();
        // The first commands sent after the return reach the server a client frame, the latency
        // and its jitter, and a tick later at most.
        const double freshAt = InCase.Up ? returnAt + 1.0 / static_cast<double>( InCase.Fps ) + 1.0 / InCase.TickRate + ( InLink.LatencyMs + InLink.JitterMs ) / 1000.0 : cutAt;
        report.SettleSeconds = probe.SettledAfter( returnAt );
        report.OwnMisses = probe.Misses( cutAt, freshAt );
        report.ExtraMisses = probe.Misses( freshAt, net.Now() + 1.0 );
        report.WorstStaleMs = probe.WorstStaleMs( cutAt, net.Now() + 1.0 );
        report.BaselineStaleMs = probe.WorstStaleMs( cutAt - 5.0, cutAt );
        report.MaxTarget = probe.MaxTarget( cutAt, net.Now() + 1.0 );
        report.MaxTargetFirstSecond = probe.MaxTarget( cutAt, freshAt + 1.0 );
        report.BaselineMisses = probe.Misses( cutAt - 10.0, cutAt );
        report.ExtraMissesFirstSecond = probe.Misses( freshAt, freshAt + 1.0 );
        if( peerClient && peerRemote )
        {
            report.PeerResyncs = peerClient->GetClock().GetResyncCount() - peerResyncsBefore;
            report.PeerStalls = peerClient->GetStallCount() - peerStallsBefore;
            report.PeerMisses = peerRemote->Commands.GetMissedCount() - peerMissesBefore;
        }
        return report;
    }

    std::string Describe( const OutageReport& InReport )
    {
        std::ostringstream text;
        text << InReport.Resyncs << " resyncs, " << InReport.Stalls << " stalls, " << InReport.Outages << " outages, settled " << InReport.SettleSeconds
             << " s after the return, misses " << InReport.OwnMisses << " own + " << InReport.ExtraMisses << " extra, worst stale " << InReport.WorstStaleMs
             << " ms (before " << InReport.BaselineStaleMs << "), target " << InReport.TargetBefore << " -> " << InReport.TargetAfter << " (deepest " << InReport.MaxTarget
             << ", " << InReport.MaxTargetFirstSecond << " in the first second back)";
        return text.str();
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


TEST_CASE( "Net: a stalled report holds the clock, and a quiet clock stops steering" )
{
    NetClock clock;
    clock.Start( 100 );
    clock.OnSlackReport( 5.f );     // 2 ticks ahead: slowing
    REQUIRE( clock.GetDilation() < 0.f );
    // A stall: no dilation, and neither the tick, the target nor the resync count moves (whether
    // the gap's misses grow the target is the session's call: OnMisses).
    const float before = clock.GetTargetSlack();
    clock.OnStall();
    CHECK( clock.GetDilation() == 0.f );
    CHECK( clock.GetTargetSlack() == doctest::Approx( before ) );
    CHECK( clock.GetResyncCount() == 0 );
    CHECK( clock.GetTick() == 100 );

    // No report for QuietTicks: the dilation goes, and the quiet ticks don't count as clean.
    NetClock quiet;
    quiet.Start( 0 );
    quiet.OnSlackReport( 1.f );
    REQUIRE( quiet.GetDilation() > 0.f );
    for( uint32_t i = 0; i < quiet.GetSettings().QuietTicks; ++i )
    {
        quiet.TakeTick();
    }
    CHECK( quiet.GetDilation() > 0.f );
    quiet.TakeTick();
    CHECK( quiet.GetDilation() == 0.f );
    for( int i = 0; i < 60 * 10; ++i )
    {
        quiet.TakeTick();
    }
    CHECK( quiet.GetTargetSlack() == doctest::Approx( quiet.GetSettings().TargetSlack ) );
}


TEST_CASE( "Net: a clock behind jumps to the target at once, and a resync's cooldown neither steers nor grows the target" )
{
    // Behind by more than BehindTicks, every command arrives late: dilating back at 5% would take
    // 20 ticks a tick. Both the smoothed slack and the slack the newest command arrived with must
    // say so (the smoothed one lags a gap's end), and the jump goes by the lesser of the two.
    NetClock clock;
    clock.Start( 100 );
    REQUIRE( clock.GetTargetSlack() == doctest::Approx( 3.f ) );
    clock.OnSlackReport( -2.f, 0.f );   // the newest command arrived on time: dilate
    CHECK( clock.GetResyncCount() == 0 );
    CHECK( clock.GetDilation() == doctest::Approx( 0.05f ) );
    clock.OnSlackReport( -3.f, -2.f );  // both: 2 behind, 5 short of the target
    CHECK( clock.GetResyncCount() == 1 );
    CHECK( clock.GetTick() == 105 );

    // The cooldown: reports are ignored, and so are misses (the jump's, not the buffer's).
    CHECK( clock.InCooldown() );
    clock.OnMisses( 4 );
    CHECK( clock.GetTargetSlack() == doctest::Approx( 3.f ) );
    clock.OnSlackReport( -5.f, -5.f );
    CHECK( clock.GetResyncCount() == 1 );
    for( uint32_t i = 0; i < clock.GetSettings().ResyncCooldownTicks; ++i )
    {
        clock.TakeTick();
    }
    CHECK_FALSE( clock.InCooldown() );
    clock.OnMisses( 2 );
    CHECK( clock.GetTargetSlack() == doctest::Approx( 5.f ) );

    // Ahead never counts as behind, however the two differ.
    NetClock ahead;
    ahead.Start( 100 );
    ahead.OnSlackReport( 4.f, -3.f );
    CHECK( ahead.GetResyncCount() == 0 );
    CHECK( ahead.GetDilation() < 0.f );

    // Under BehindTicks it dilates; told to jump if behind at all (a client back from its own
    // hitch, ME-313), it jumps.
    NetClock slightly;
    slightly.Start( 100 );
    slightly.OnSlackReport( -0.75f, -0.75f );
    CHECK( slightly.GetResyncCount() == 0 );
    slightly.OnSlackReport( -0.75f, -0.75f, 0.f );
    CHECK( slightly.GetResyncCount() == 1 );
    CHECK( slightly.GetTick() == 104 );
    NetClock onTime;
    onTime.Start( 100 );
    onTime.OnSlackReport( -0.5f, 0.25f, 0.f );
    CHECK( onTime.GetResyncCount() == 0 );
}


TEST_CASE( "Net: the server's buffer follows the client's clock back, keeps reordered packets to their gaps, and doesn't count ticks before the first command" )
{
    auto command = []( uint32_t InTick, float InAxis ) {
        InputCommand c;
        c.Tick = InTick;
        c.Axes[0] = InAxis;
        return c;
    };
    auto packet = [&command]( uint32_t InFrom, uint32_t InTo, float InAxis ) {
        std::vector<InputCommand> commands;
        for( uint32_t tick = InFrom; tick <= InTo; ++tick )
        {
            commands.push_back( command( tick, InAxis ) );
        }
        return commands;
    };
    NetCommandBuffer buffer;
    // The client's clock starts a few ticks ahead: the ticks before its first command are idle,
    // not misses.
    std::vector<InputCommand> first = packet( 13, 13, 0.1f );
    buffer.ReceivePacket( first.data(), first.size(), 10, true );
    CHECK( buffer.Consume( 10 ).Axes[0] == 0.f );
    CHECK( buffer.Consume( 11 ).Axes[0] == 0.f );
    CHECK( buffer.Consume( 12 ).Axes[0] == 0.f );
    CHECK( buffer.GetMissedCount() == 0 );
    CHECK( buffer.Consume( 13 ).Axes[0] == 0.1f );
    CHECK( buffer.GetOnTimeCount() == 1 );

    // Ahead through tick 40 (the old timeline: axis 0.2).
    std::vector<InputCommand> ahead = packet( 25, 40, 0.2f );
    buffer.ReceivePacket( ahead.data(), ahead.size(), 14, true );
    CHECK( buffer.GetNewestTick() == 40 );
    // The client's clock steps back to 20 and makes ticks again (axis 0.3): the latest packet
    // ends on 20, so everything past it goes, and its ticks replace the old ones.
    std::vector<InputCommand> back = packet( 14, 20, 0.3f );
    buffer.ReceivePacket( back.data(), back.size(), 14, true );
    CHECK( buffer.GetRewindCount() == 1 );
    CHECK( buffer.GetNewestTick() == 20 );
    CHECK( buffer.GetBuffered() == 7 );
    // A reordered packet from before the step back fills nothing past the newest tick.
    buffer.ReceivePacket( ahead.data(), ahead.size(), 14, false );
    CHECK( buffer.GetBuffered() == 7 );
    CHECK( buffer.GetNewestTick() == 20 );
    std::vector<InputCommand> next = packet( 21, 22, 0.3f );
    buffer.ReceivePacket( next.data(), next.size(), 14, true );
    for( uint32_t tick = 14; tick <= 22; ++tick )
    {
        CHECK( buffer.Consume( tick ).Axes[0] == 0.3f );
    }
    CHECK( buffer.GetMissedCount() == 0 );

    // A reordered older packet still fills a gap below the newest.
    NetCommandBuffer gaps;
    std::vector<InputCommand> newer = packet( 52, 53, 0.4f );
    std::vector<InputCommand> older = packet( 50, 51, 0.4f );
    gaps.ReceivePacket( newer.data(), newer.size(), 50, true );
    gaps.ReceivePacket( older.data(), older.size(), 50, false );
    CHECK( gaps.GetBuffered() == 4 );
    CHECK( gaps.GetRewindCount() == 0 );

    // Late commands count towards the newest tick: a client running behind reads as behind
    // (negative slack) rather than as silent.
    NetCommandBuffer late;
    std::vector<InputCommand> behind = packet( 60, 62, 0.5f );
    late.ReceivePacket( behind.data(), behind.size(), 70, true );
    CHECK( late.HasReceived() );
    CHECK( late.GetNewestTick() == 62 );
    CHECK( late.GetLateArrivals() == 3 );
    late.Consume( 70 );
    CHECK( late.GetSlack() == doctest::Approx( -8.f ) );
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
        << ", target slack " << client.GetClock().GetTargetSlack() << ", stalled reports " << client.GetStalledReports() );
    CHECK( onTime + missed >= 3500 );
    CHECK( static_cast<double>( onTime ) / static_cast<double>( onTime + missed ) >= 0.999 );
    CHECK( worstSlackError <= 1.f );
    CHECK( worstDilation <= 0.05f + 1e-6f );
    CHECK( client.GetClock().GetResyncCount() == resyncsBefore );
    CHECK( client.GetStallCount() == 0 );
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

TEST_CASE( "Net: a missing command repeats whole for a quarter second, then lets go of buttons and axes but keeps the aim" )
{
    // Move + fire held, looking somewhere, the view 3.5 ticks behind.
    auto holding = []( uint32_t InTick, uint32_t InButtons = 0x5u ) {
        InputCommand c;
        c.Tick = InTick;
        c.Buttons = InButtons;
        c.Axes = { 0.f, 1.f, -0.5f, 0.25f };
        c.ViewTick = InTick - 3.5;
        c.HasAim = true;
        c.Yaw = 123.f;
        c.Pitch = -20.f;
        return c;
    };
    // What a whole repeat of InLast at InTick is: the same command, only its tick moved on.
    auto repeatOf = []( InputCommand InLast, uint32_t InTick ) {
        InLast.Tick = InTick;
        return InLast;
    };
    const std::array<float, 4> centred{};
    NetCommandBuffer buffer;
    REQUIRE( buffer.GetMaxRepeats() == NetCommandBuffer::kDefaultMaxRepeats );
    const uint32_t cap = buffer.GetMaxRepeats();
    buffer.Receive( holding( 10 ), 10 );
    CHECK( buffer.Consume( 10 ) == holding( 10 ) );

    SUBCASE( "a short gap repeats exactly as before" )
    {
        // Up to the cap, every miss is the last command whole.
        for( uint32_t tick = 11; tick <= 10 + cap; ++tick )
        {
            CHECK( buffer.Consume( tick ) == repeatOf( holding( 10 ), tick ) );
        }
        CHECK( buffer.GetRepeatRun() == cap );
        CHECK( buffer.GetMissedCount() == cap );
        CHECK( buffer.GetNeutralCount() == 0 );
        // A command in time ends the run, and the next gap gets the whole allowance again.
        const uint32_t resume = 11 + cap;
        buffer.Receive( holding( resume ), resume );
        CHECK( buffer.Consume( resume ) == holding( resume ) );
        CHECK( buffer.GetRepeatRun() == 0 );
        for( uint32_t tick = resume + 1; tick <= resume + cap; ++tick )
        {
            CHECK( buffer.Consume( tick ) == repeatOf( holding( resume ), tick ) );
        }
        CHECK( buffer.GetNeutralCount() == 0 );
    }

    SUBCASE( "a long gap lets go after the cap, keeping aim and view" )
    {
        for( uint32_t tick = 11; tick <= 10 + cap; ++tick )
        {
            CHECK( buffer.Consume( tick ) == repeatOf( holding( 10 ), tick ) );
        }
        // A frozen client: ten seconds of nothing, the session timeout's worth.
        bool neutral = true;
        for( uint32_t tick = 11 + cap; tick < 610; ++tick )
        {
            InputCommand expected = repeatOf( holding( 10 ), tick );
            expected.Buttons = 0;
            expected.Axes = {};
            neutral = neutral && buffer.Consume( tick ) == expected;
        }
        CHECK( neutral );
        CHECK( buffer.GetNeutralCount() == 610 - 11 - cap );
        CHECK( buffer.GetMissedCount() == 610 - 11 );
        const InputCommand last = buffer.Consume( 610 );
        CHECK( last.Buttons == 0 );
        CHECK( last.Axes == centred );
        CHECK( last.HasAim );
        CHECK( last.Yaw == 123.f );
        CHECK( last.Pitch == -20.f );
        CHECK( last.ViewTick == holding( 10 ).ViewTick );     // what a lag-compensated shot would name
    }

    SUBCASE( "recovery after a long gap is clean" )
    {
        for( uint32_t tick = 11; tick <= 40; ++tick )
        {
            buffer.Consume( tick );
        }
        REQUIRE( buffer.GetNeutralCount() == 40 - 10 - cap );
        // The client is back: its gap's commands arrive late, still holding move + fire, with a
        // tap of bit 8 at tick 35; then tick 41's in time.
        for( uint32_t tick = 30; tick <= 40; ++tick )
        {
            buffer.Receive( holding( tick, tick == 35 ? 0xDu : 0x5u ), 41 );
        }
        buffer.Receive( holding( 41 ), 41 );
        // Holding through the gap isn't a press, even where the repeat had let go; the tap is.
        CHECK( buffer.GetRecoveredPresses() == 1 );
        InputCommand withTap = holding( 41 );
        withTap.Buttons |= 0x8u;
        CHECK( buffer.Consume( 41 ) == withTap );
        CHECK( buffer.GetRepeatRun() == 0 );
        // A miss straight after repeats tick 41 whole: no tap, and buttons and axes held again.
        CHECK( buffer.Consume( 42 ) == repeatOf( holding( 41 ), 42 ) );
        CHECK( buffer.GetNeutralCount() == 40 - 10 - cap );
    }

    SUBCASE( "a cap of 0 lets go from the first miss" )
    {
        buffer.SetMaxRepeats( 0 );
        const InputCommand first = buffer.Consume( 11 );
        CHECK( first.Buttons == 0 );
        CHECK( first.Axes == centred );
        CHECK( first.HasAim );
        CHECK( first.Yaw == 123.f );
    }
}

TEST_CASE( "Net: the server sets the repeat cap from its tick rate, for every client, now and later" )
{
    LoopbackNetwork network;
    LoopbackTransport raw( network, kServer );
    CHECK( NetServer( raw, NetVersion(), 60 ).GetMaxCommandRepeats() == 15 );
    CHECK( NetServer( raw, NetVersion(), 30 ).GetMaxCommandRepeats() == 8 );
    CHECK( NetServer( raw, NetVersion(), 128 ).GetMaxCommandRepeats() == 32 );
    CHECK( NetServer( raw, NetVersion(), 2 ).GetMaxCommandRepeats() == 1 );

    Harness net( Link( 20.f, 0.f, 0.f, 1 ), Link( 20.f, 0.f, 0.f, 2 ) );
    net.Start();
    net.Run( 1.0 );
    NetServer& server = *net.ServerNet.GetServer();
    REQUIRE( server.GetClientCount() == 1 );
    CHECK( server.GetClients().front()->Commands.GetMaxRepeats() == 15 );
    server.SetMaxCommandRepeats( 30 );
    CHECK( server.GetMaxCommandRepeats() == 30 );
    CHECK( server.GetClients().front()->Commands.GetMaxRepeats() == 30 );
    Peer& late = net.AddClient( Link( 20.f, 0.f, 0.f, 3 ) );
    net.Run( 1.0 );
    REQUIRE( late.Net.GetClient()->GetState() == NetClient::State::Connected );
    const NetServer::Client* lateRemote = server.FindClient( late.Net.GetClient()->GetClientId() );
    REQUIRE( lateRemote != nullptr );
    CHECK( lateRemote->Commands.GetMaxRepeats() == 30 );
}

TEST_CASE( "Net: a client that goes silent holding move and fire stops on the server after a quarter second, aim kept, and resumes cleanly" )
{
    Harness net( Link( 20.f, 2.f, 0.f, 21 ), Link( 20.f, 2.f, 0.f, 22 ) );
    net.MakeCommand = []( uint32_t ) {
        InputCommand c;
        c.Buttons = 0x5u;
        c.Axes[1] = 1.f;
        c.HasAim = true;
        c.Yaw = 45.f;
        c.Pitch = 10.f;
        return c;
    };
    struct Seen
    {
        InputCommand Command;
        uint32_t RepeatRun = 0;
    };
    std::vector<Seen> seen;
    net.OnServerTick = [&seen]( NetServer& InServer ) {
        const NetServer::Client* c = InServer.FindClient( 1 );
        if( c && c->Commands.HasReceived() )
        {
            seen.push_back( { c->Current, c->Commands.GetRepeatRun() } );
        }
    };
    net.Start();
    net.Run( 3.0 );
    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
    REQUIRE( remote != nullptr );
    const uint32_t cap = net.ServerNet.GetServer()->GetMaxCommandRepeats();
    CHECK( remote->Commands.GetNeutralCount() == 0 );
    REQUIRE( !seen.empty() );
    CHECK( seen.back().Command.Buttons == 0x5u );

    // The client freezes (nothing reaches the server) for a second and a half.
    const int resyncsBefore = client.GetClock().GetResyncCount();
    const size_t outageStart = seen.size();
    net.ClientLink.SetConditions( Link( 20.f, 2.f, 1.f, 23 ) );
    net.Run( 1.5 );
    net.ClientLink.SetConditions( Link( 20.f, 2.f, 0.f, 24 ) );
    const size_t outageEnd = seen.size();
    // Until the cap a miss is the held command; past it, the pawn lets go, still aiming.
    const float forward = NetCommands::QuantizeAxis( 1.f );
    const std::array<float, 4> centred{};
    auto check = [&]( size_t InFrom, size_t InTo, size_t& OutWhole, size_t& OutNeutral ) {
        bool consistent = true;
        OutWhole = OutNeutral = 0;
        for( size_t i = InFrom; i < InTo; ++i )
        {
            const Seen& s = seen[i];
            if( s.RepeatRun > cap )
            {
                ++OutNeutral;
                consistent = consistent && s.Command.Buttons == 0 && s.Command.Axes == centred && s.Command.HasAim
                    && std::abs( s.Command.Yaw - 45.f ) < 0.01f && std::abs( s.Command.Pitch - 10.f ) < 0.01f;
            }
            else
            {
                ++OutWhole;
                consistent = consistent && s.Command.Buttons == 0x5u && s.Command.Axes[1] == forward;
            }
        }
        return consistent;
    };
    size_t whole = 0;
    size_t neutral = 0;
    CHECK( check( outageStart, outageEnd, whole, neutral ) );
    MESSAGE( "outage: " << whole << " ticks held, " << neutral << " neutral (cap " << cap << ")" );
    // 90 ticks without a command, less the few buffered and in flight, less the cap.
    CHECK( whole >= cap );
    CHECK( neutral >= 90 - cap - 10 );
    CHECK( neutral == remote->Commands.GetNeutralCount() );

    // Back: the held command again, and holding through the gap recovered no presses. The clock
    // held still meanwhile (the server's reports said it wasn't hearing the client, so its sinking
    // slack steered nothing): no resyncs, and once commands got through again (a frame and the
    // latency later) no more misses. Throughout, a miss within the cap is still the held command
    // and one past it neutral.
    net.Run( 10.0 );
    CHECK( client.GetState() == NetClient::State::Connected );
    size_t wholeAfter = 0;
    size_t neutralAfter = 0;
    CHECK( check( outageEnd, seen.size(), wholeAfter, neutralAfter ) );
    CHECK( remote->Commands.GetRepeatRun() == 0 );
    CHECK( remote->Commands.GetRecoveredPresses() == 0 );
    CHECK( seen.back().Command.Buttons == 0x5u );
    CHECK( seen.back().Command.Axes[1] == forward );
    size_t missesAfter = 0;
    for( size_t i = outageEnd + 4; i < seen.size(); ++i )
    {
        missesAfter += seen[i].RepeatRun > 0 ? 1 : 0;
    }
    MESSAGE( "after: " << wholeAfter << " ticks held, " << neutralAfter << " neutral, " << client.GetClock().GetResyncCount() - resyncsBefore
        << " resyncs, " << missesAfter << " misses; in all neutral " << remote->Commands.GetNeutralCount() << ", missed " << remote->Commands.GetMissedCount() );
    CHECK( client.GetClock().GetResyncCount() == resyncsBefore );
    CHECK( client.GetStallCount() == 1 );
    CHECK( missesAfter == 0 );
    CHECK( neutral + neutralAfter == remote->Commands.GetNeutralCount() );
    CHECK( net.ServerNet.GetServer()->GetTimeoutCount() == 0 );
}


TEST_CASE( "Net: after a link outage up, down or both ways, the clock holds still and the server runs no stale commands" )
{
    // While the server can't hear the client its slack sinks with the silence: the reports say so
    // (a stall) and steer nothing, so when the link comes back the commands land where they did.
    // The gap outlasts any buffer (an outage), so its misses don't deepen the target, not even for
    // a moment. Up to 9 s (the timeout is 10): the server takes the client's commands however far
    // past the last it heard they are.
    struct Case
    {
        const char* Name;
        double Seconds;
        bool Up;
        bool Down;
    };
    const Case cases[] = {
        { "up 0.3 s", 0.3, true, false }, { "up 1.5 s", 1.5, true, false }, { "up 3 s", 3.0, true, false }, { "up 5 s", 5.0, true, false }, { "up 9 s", 9.0, true, false },
        { "down 0.3 s", 0.3, false, true }, { "down 1.5 s", 1.5, false, true }, { "down 3 s", 3.0, false, true },
        { "both 0.3 s", 0.3, true, true }, { "both 1.5 s", 1.5, true, true }, { "both 3 s", 3.0, true, true },
    };
    struct LinkCase
    {
        const char* Name;
        NetConditions Link;
    };
    const LinkCase links[] = { { "20+-2 ms", Link( 20.f, 2.f, 0.f, 0 ) }, { "50+-10 ms 5% loss", Link( 50.f, 10.f, 0.05f, 0 ) } };
    uint64_t seed = 40;
    for( const LinkCase& link : links )
    {
        for( const Case& c : cases )
        {
            const std::string name = std::string( link.Name ) + ", " + c.Name;
            CAPTURE( name );
            OutageCase outage;
            outage.Seconds = c.Seconds;
            outage.Up = c.Up;
            outage.Down = c.Down;
            const OutageReport report = MeasureOutage( outage, link.Link, seed );
            seed += 10;
            MESSAGE( name << ": " << Describe( report ) );
            CHECK( report.Connected );
            CHECK( report.Resyncs == 0 );
            // One stall and one outage when the server stopped hearing the client; a down-only cut
            // is neither.
            CHECK( report.Stalls == ( c.Up ? 1u : 0u ) );
            CHECK( report.Outages == ( c.Up ? 1u : 0u ) );
            CHECK( report.Hitches == 0u );
            CHECK( report.SettleSeconds <= 0.25 );
            CHECK( report.ExtraMisses == 0 );
            // No command older than the steady state's (a tick of rounding).
            CHECK( report.WorstStaleMs <= report.BaselineStaleMs + 1000.0 / 60.0 );
            // The outage's misses don't deepen the buffer, at any point.
            CHECK( report.MaxTarget <= report.TargetBefore );
            CHECK( report.TargetAfter <= report.TargetBefore );
        }
    }
}


TEST_CASE( "Net: outages hold the clock at 20 and 30 fps and at 128 Hz, and a second client rides through untouched" )
{
    struct Case
    {
        const char* Name;
        OutageCase Outage;
    };
    auto make = []( double InSeconds, bool InUp, bool InDown, int64_t InFps, uint16_t InTickRate, bool InPeer ) {
        OutageCase c;
        c.Seconds = InSeconds;
        c.Up = InUp;
        c.Down = InDown;
        c.Fps = InFps;
        c.TickRate = InTickRate;
        c.WithPeer = InPeer;
        return c;
    };
    const Case cases[] = {
        { "20 fps, up 1.5 s", make( 1.5, true, false, 20, 60, false ) }, { "20 fps, both 1.5 s", make( 1.5, true, true, 20, 60, false ) },
        { "30 fps, up 1.5 s", make( 1.5, true, false, 30, 60, false ) }, { "30 fps, both 1.5 s", make( 1.5, true, true, 30, 60, false ) },
        { "128 Hz, up 0.3 s", make( 0.3, true, false, 60, 128, false ) }, { "128 Hz, up 2.5 s", make( 2.5, true, false, 60, 128, false ) },
        { "128 Hz, both 1.5 s", make( 1.5, true, true, 60, 128, false ) },
        { "a second client, up 1.5 s", make( 1.5, true, false, 60, 60, true ) }, { "a second client, up 5 s", make( 5.0, true, false, 60, 60, true ) },
    };
    struct LinkCase
    {
        const char* Name;
        NetConditions Link;
    };
    const LinkCase links[] = { { "20+-2 ms", Link( 20.f, 2.f, 0.f, 0 ) }, { "50+-10 ms 5% loss", Link( 50.f, 10.f, 0.05f, 0 ) } };
    uint64_t seed = 400;
    for( const LinkCase& link : links )
    {
        for( const Case& c : cases )
        {
            const std::string name = std::string( link.Name ) + ", " + c.Name;
            CAPTURE( name );
            const OutageReport report = MeasureOutage( c.Outage, link.Link, seed );
            seed += 10;
            MESSAGE( name << ": " << Describe( report ) );
            if( c.Outage.WithPeer )
            {
                MESSAGE( "  the second client: " << report.PeerResyncs << " resyncs, " << report.PeerStalls << " stalls, " << report.PeerMisses << " misses (" << report.PeerBaselineMisses << " in the 10 s before), target "
                    << report.PeerTargetBefore << " (deepest " << report.PeerMaxTarget << ")" );
            }
            const double frame = 1000.0 / static_cast<double>( std::min<int64_t>( c.Outage.Fps, c.Outage.TickRate ) );
            const bool lossy = link.Link.Loss > 0.f;
            CHECK( report.Connected );
            CHECK( report.Resyncs == 0 );
            // An uplink-only cut shows the client a stall; a both-ways one may not (the reports
            // from while the server waited were lost, or arrived with ones after it heard again).
            CHECK( report.Stalls >= ( c.Outage.Down ? 0u : 1u ) );
            CHECK( report.Stalls <= 2u );
            CHECK( report.Outages == 1u );
            CHECK( report.Hitches == 0u );
            if( lossy )
            {
                // At 20 or 30 fps, or 128 Hz, a lossy link misses now and then anyway (a lost packet is
                // a gap of a few ticks): none in the first second back, and about as many as before
                // the cut after that. The outage doesn't deepen the target; those later misses do, as
                // they would have without it (NET-14), so the target is checked up to then.
                CHECK( report.ExtraMissesFirstSecond == 0 );
                CHECK( report.ExtraMisses <= report.BaselineMisses + 2 );
                CHECK( report.MaxTargetFirstSecond <= report.TargetBefore );
            }
            else
            {
                CHECK( report.SettleSeconds <= 0.25 );
                CHECK( report.ExtraMisses == 0 );
                CHECK( report.MaxTarget <= report.TargetBefore );
            }
            CHECK( report.WorstStaleMs <= report.BaselineStaleMs + frame );
            if( c.Outage.WithPeer )
            {
                // Nothing of the first client's outage reaches the second.
                CHECK( report.PeerResyncs == 0 );
                CHECK( report.PeerStalls == 0u );
                CHECK( report.PeerMisses <= ( lossy ? report.PeerBaselineMisses + 1 : 0u ) );
                CHECK( report.PeerMaxTarget <= report.PeerTargetBefore );
            }
        }
    }
}


TEST_CASE( "Net: a client that hitches comes back without a resync when it catches up, and with one when it comes back behind" )
{
    // The engine runs at most 8 steps a frame and drops the rest: after a long hitch the clock is
    // behind, its commands late. The newest command the server reports counts late ones, so the
    // client reads how far behind it is and jumps once, filling the ticks it skips. A 0.18-0.25 s
    // hitch comes back 3 to 7 ticks behind, inside ResyncTicks: dilating that back at 5% took
    // a second or more of late commands, each a miss that grew the target, until it was deep
    // enough to resync anyway (to 7 ticks after 0.18 s, easing back over 20 s). Behind by more
    // than BehindTicks, it jumps at once.
    // Shorter hitches, caught up in full, leave gaps a deeper buffer would have covered, but
    // they're the client's own (ME-313): it knows it sent late, and the server's misses on the
    // ticks it made late don't grow the target (they took it to 4, 6 and 8 for 0.05, 0.1 and
    // 0.15-0.17 s, easing back over 20 s and more). Nor does the round trip of a packet it read
    // late (it took the jitter floor, and the target, to 2 after 0.05 s). Just past the engine's
    // catch-up (0.16-0.17 s with 8 steps) it comes back under a tick behind, inside BehindTicks:
    // back from its own hitch, it jumps if behind at all (dilating back missed a tick a tick,
    // and took the target to 7).
    struct Case
    {
        int CatchUp;
        double Hitch;
    };
    for( const Case c : { Case{ 1000, 0.05 }, Case{ 1000, 0.1 }, Case{ 1000, 0.15 }, Case{ 1000, 0.17 }, Case{ 8, 0.05 }, Case{ 8, 0.1 }, Case{ 8, 0.15 },
             Case{ 8, 0.16 }, Case{ 8, 0.17 }, Case{ 1000, 0.3 }, Case{ 1000, 1.0 }, Case{ 8, 0.18 }, Case{ 8, 0.2 }, Case{ 8, 0.25 }, Case{ 8, 0.3 }, Case{ 8, 1.0 } } )
    {
        const int catchUp = c.CatchUp;
        const double hitch = c.Hitch;
        CAPTURE( catchUp );
        CAPTURE( hitch );
        Harness net( Link( 20.f, 2.f, 0.f, 61 ), Link( 20.f, 2.f, 0.f, 62 ) );
        ClockProbe probe;
        probe.Attach( net );
        net.Start();
        net.Run( 12.0 );
        NetClient& client = *net.ClientNet.GetClient();
        REQUIRE( client.GetState() == NetClient::State::Connected );
        const int resyncsBefore = client.GetClock().GetResyncCount();
        const float targetBefore = client.GetClock().GetTargetSlack();
        const double hitchAt = net.Now();
        net.ClientCatchUpTicks = catchUp;
        net.ClientHitchUntil = hitchAt + hitch;
        net.Run( hitch );
        const double backAt = net.Now();
        net.Run( 10.0 );
        const int resyncs = client.GetClock().GetResyncCount() - resyncsBefore;
        // Its own misses: until the first commands after the hitch could arrive (a frame, the
        // latency and its jitter, and a tick). Late ones after that are the clock's.
        const double freshAt = backAt + 1.0 / 60.0 + 0.022 + 1.0 / 60.0;
        const double settled = probe.SettledAfter( backAt );
        const uint64_t late = probe.Misses( freshAt, net.Now() + 1.0 );
        const uint64_t after = probe.Misses( backAt + 0.1, net.Now() + 1.0 );
        const double stale = probe.WorstStaleMs( hitchAt, net.Now() + 1.0 );
        const double staleBefore = probe.WorstStaleMs( hitchAt - 5.0, hitchAt );
        const float deepest = probe.MaxTarget( hitchAt, net.Now() + 1.0 );
        MESSAGE( "hitch " << hitch << " s, catch-up " << catchUp << " ticks: " << resyncs << " resyncs, " << client.GetStallCount() << " stalls, "
            << client.GetOutageCount() << " outages, " << client.GetHitchCount() << " hitches (" << client.GetHitchMisses() << " misses its own), settled " << settled << " s after, misses " << probe.Misses( hitchAt, freshAt ) << " own + " << late
            << " late (" << after << " after 0.1 s), worst stale " << stale << " ms (before " << staleBefore << "), target " << targetBefore << " -> "
            << client.GetClock().GetTargetSlack() << " (deepest " << deepest << ")" );
        // Past MaxSlack and the usual wait (0.18 s and more), the gap is an outage. Past 8 steps
        // and a frame (0.15 s), the engine's catch-up leaves it behind.
        const bool outage = hitch > 0.175;
        const bool behind = catchUp < 100 && hitch > 0.155;
        CHECK( client.GetState() == NetClient::State::Connected );
        CHECK( resyncs == ( behind ? 1 : 0 ) );
        // 0.05 s is inside the usual wait: no stall.
        CHECK( client.GetStallCount() == ( hitch > 0.075 ? 1u : 0u ) );
        CHECK( client.GetOutageCount() == ( outage ? 1u : 0u ) );
        // The client saw its own hitch, and every miss of a short one caught up in full was its own.
        CHECK( client.GetHitchCount() == 1 );
        if( !outage && !behind )
        {
            CHECK( client.GetHitchMisses() == probe.Misses( hitchAt, net.Now() + 1.0 ) );
        }
        // A jump of under 4 ticks the server's smoothed slack follows over 10 ticks or so (it
        // takes a bigger one at once).
        CHECK( settled <= 0.25 );
        // Late commands, past the hitch's own misses: the one tick before the jump lands.
        CHECK( late <= 2 );
        CHECK( after == 0 );
        CHECK( stale <= staleBefore + 1000.0 / 60.0 );
        CHECK( deepest <= targetBefore );
        CHECK( client.GetClock().GetTargetSlack() <= targetBefore );
    }
}


TEST_CASE( "Net: a server that hitches and catches up leaves its clients' clocks alone" )
{
    // After a hitch a Debug server runs the ticks it owes back to back, a few ms each, its
    // reports going out meanwhile. Against the step it's on, the slack reads as the client far
    // ahead; against where it will be once caught up (NetCore::BeginTick's ticks owed), it's
    // what it was. (Measured against the step, a 0.3 s hitch cost 2 resyncs back and forth and
    // 44 misses.) It catches up on half a second at most, as Engine::Run does: past that the
    // ticks are lost, which its clients read as their clocks that far ahead (one resync back
    // each, the server dropping what they had made past it, and nothing run they took back).
    // Engine::Run runs 8 steps a frame, a frame a tick: the reports the client steers by then
    // still have ticks owed after them, so past the +/-64 ticks the slack's quantized range
    // holds (a hitch of 1.5 s or more) the slack goes in full, measured the same way.
    for( int stepsPerFrame : { 0, 8 } )
    {
        for( int64_t cost : { int64_t( 0 ), int64_t( 3 ), int64_t( 8 ) } )
        {
            for( double hitch : { 0.15, 0.3, 0.5, 1.0, 1.5, 3.0 } )
            {
                CAPTURE( stepsPerFrame );
                CAPTURE( cost );
                CAPTURE( hitch );
                Harness net( Link( 5.f, 1.f, 0.f, 101 ), Link( 5.f, 1.f, 0.f, 102 ) );
                ClockProbe probe;
                probe.Attach( net );
                net.Start();
                net.Run( 12.0 );
                NetClient& client = *net.ClientNet.GetClient();
                REQUIRE( client.GetState() == NetClient::State::Connected );
                const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
                REQUIRE( remote != nullptr );
                const int resyncsBefore = client.GetClock().GetResyncCount();
                const uint64_t missedBefore = remote->Commands.GetMissedCount();
                const float targetBefore = client.GetClock().GetTargetSlack();
                const double hitchAt = net.Now();
                net.ServerCatchUpMs = cost;
                net.ServerStepsPerFrame = stepsPerFrame;
                net.ServerHitchUntil = hitchAt + hitch;
                net.Run( 10.0 );
                const int resyncs = client.GetClock().GetResyncCount() - resyncsBefore;
                const uint64_t misses = remote->Commands.GetMissedCount() - missedBefore;
                // Past the resync threshold: the clients jump back.
                const bool lost = static_cast<float>( net.GetServerTicksLost() ) > client.GetClock().GetSettings().ResyncTicks;
                MESSAGE( "server hitch " << hitch << " s, " << cost << " ms a tick catching up, " << ( stepsPerFrame ? std::to_string( stepsPerFrame ) : std::string( "all" ) )
                    << " steps a frame: " << net.GetServerTicksLost() << " ticks lost, " << resyncs << " resyncs, " << misses << " misses, " << remote->Commands.GetRewindCount()
                    << " rewinds, " << probe.Superseded() << " taken back, settled " << probe.SettledAfter( hitchAt + hitch ) << " s after, target " << targetBefore << " -> "
                    << client.GetClock().GetTargetSlack() << " (deepest " << probe.MaxTarget( hitchAt, net.Now() + 1.0 ) << ")" );
                CHECK( lost == ( hitch > NetServer::kMaxCatchUpSeconds + 0.01 ) );
                if( hitch <= NetServer::kMaxCatchUpSeconds )
                {
                    CHECK( net.GetServerTicksLost() <= 1u );     // the phase of the tick it stopped on
                }
                // One resync, back (the server rewinds), and no second one forward.
                CHECK( resyncs == ( lost ? 1 : 0 ) );
                CHECK( misses == 0 );
                CHECK( remote->Commands.GetRewindCount() == ( lost ? 1u : 0u ) );
                CHECK( probe.Superseded() == 0 );
                CHECK( probe.SettledAfter( hitchAt + hitch ) <= ( lost ? 1.0 : 0.25 ) );
                CHECK( probe.MaxTarget( hitchAt, net.Now() + 1.0 ) <= targetBefore );
            }
        }
    }
}


TEST_CASE( "Net: a server too slow to keep up keeps no backlog, so its clients' input waits no longer than the clock's own leeway" )
{
    // Its ticks take 20 ms (50 a second, not 60). Holding the backlog would hold the clients'
    // clocks ahead of what it simulates by that much; dropping it, they follow where it is: a
    // resync back each time they get ResyncTicks ahead, and their commands wait no longer than that.
    Harness net( Link( 20.f, 2.f, 0.f, 111 ), Link( 20.f, 2.f, 0.f, 112 ) );
    ClockProbe probe;
    probe.Attach( net );
    net.Start();
    net.Run( 12.0 );
    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    const double slowAt = net.Now();
    const double staleBefore = probe.WorstStaleMs( slowAt - 5.0, slowAt );
    net.ServerTickMs = 20;
    net.Run( 10.0 );
    const double stale = probe.WorstStaleMs( slowAt, net.Now() + 1.0 );
    MESSAGE( "a 50 Hz server: " << client.GetClock().GetResyncCount() << " resyncs, " << net.GetServerTicksLost() << " ticks of backlog dropped, worst stale " << stale
        << " ms (before " << staleBefore << "), " << probe.Superseded() << " taken back" );
    CHECK( client.GetState() == NetClient::State::Connected );
    CHECK( net.GetServerTicksLost() > 0 );
    // The clock's own leeway: up to ResyncTicks past its target before it jumps back, at 50 Hz.
    CHECK( stale <= staleBefore + ( client.GetClock().GetSettings().ResyncTicks + 2.0 ) * 20.0 );
    CHECK( probe.Superseded() == 0 );
}


TEST_CASE( "Net: when the uplink's latency jumps past the resync threshold, one resync re-places the clock and the server runs nothing the client took back" )
{
    for( bool drop : { true, false } )
    {
        CAPTURE( drop );
        // 180 ms one way is 10.8 ticks: past the 8-tick resync threshold.
        const float before = drop ? 200.f : 20.f;
        const float after = drop ? 20.f : 200.f;
        Harness net( Link( 20.f, 2.f, 0.f, 71 ), Link( before, 2.f, 0.f, 72 ) );
        ClockProbe probe;
        probe.Attach( net );
        net.Start();
        net.Run( 12.0 );
        NetClient& client = *net.ClientNet.GetClient();
        REQUIRE( client.GetState() == NetClient::State::Connected );
        const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
        REQUIRE( remote != nullptr );
        const int resyncsBefore = client.GetClock().GetResyncCount();
        const uint64_t rewindsBefore = remote->Commands.GetRewindCount();
        const double changeAt = net.Now();
        net.ClientLink.SetConditions( Link( after, 2.f, 0.f, 73 ) );
        net.Run( 10.0 );
        const int resyncs = client.GetClock().GetResyncCount() - resyncsBefore;
        const uint64_t rewinds = remote->Commands.GetRewindCount() - rewindsBefore;
        const uint64_t misses = probe.Misses( changeAt, net.Now() + 1.0 );
        const double stale = probe.WorstStaleMs( changeAt, net.Now() + 1.0 );
        const double settled = probe.SettledAfter( changeAt );
        MESSAGE( "uplink " << before << " -> " << after << " ms: " << resyncs << " resyncs, " << rewinds << " rewinds, " << client.GetStallCount() << " stalls, settled "
            << settled << " s after, misses " << misses << ", worst stale " << stale << " ms (before " << probe.WorstStaleMs( changeAt - 5.0, changeAt ) << "), target "
            << client.GetClock().GetTargetSlack() );
        CHECK( client.GetState() == NetClient::State::Connected );
        CHECK( resyncs == 1 );
        // The worst command the server ran is no older than the slower link, the deepest buffer
        // and a frame make it.
        CHECK( stale <= std::max( before, after ) + 2.0 + 1000.0 * ( 8.0 + 1.0 ) / 60.0 + 1000.0 / 60.0 );
        if( drop )
        {
            // The clock steps back: the server drops what it had past the client's new newest
            // tick (made for a timeline the client left), and the ticks made again replace it.
            CHECK( rewinds == 1 );
            CHECK( misses == 0 );
        }
        else
        {
            // Nothing arrives for the 180 ms the link got longer, then a round trip of late
            // commands until the jump's land. The step inflates the round trip's deviation, and
            // the jitter floor it sets the target to keeps rising for a sample or two after the
            // jump: the clock dilates the last few ticks.
            CHECK( misses <= 30 );
            CHECK( settled <= 2.0 );
        }
    }
}


TEST_CASE( "Net: packets reordered across a resync back never bring back a command the client took back" )
{
    // The uplink drops from 200 to 20 ms with a quarter of the packets held back 30 ms more: the
    // clock steps back (the old lead is too long), and packets from before the step keep arriving
    // after ones from after it, through the old link's tail and the reordering. The latest packet
    // sent speaks for the clock; older ones only fill gaps. And reports from before the server
    // heard the step (they are older than its first packet's ack) neither confirm the commands
    // made again nor steer.
    for( uint64_t seed : { 131u, 141u, 151u } )
    {
        CAPTURE( seed );
        NetConditions before = Link( 200.f, 2.f, 0.f, seed );
        before.Reorder = 0.25f;
        Harness net( Link( 20.f, 2.f, 0.f, seed + 1 ), before );
        ClockProbe probe;
        probe.Attach( net );
        net.Start();
        net.Run( 12.0 );
        NetClient& client = *net.ClientNet.GetClient();
        REQUIRE( client.GetState() == NetClient::State::Connected );
        const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
        REQUIRE( remote != nullptr );
        const int resyncsBefore = client.GetClock().GetResyncCount();
        const uint64_t rewindsBefore = remote->Commands.GetRewindCount();
        const double changeAt = net.Now();
        NetConditions after = Link( 20.f, 2.f, 0.f, seed + 2 );
        after.Reorder = 0.25f;
        net.ClientLink.SetConditions( after );
        net.Run( 10.0 );
        const int resyncs = client.GetClock().GetResyncCount() - resyncsBefore;
        const uint64_t rewinds = remote->Commands.GetRewindCount() - rewindsBefore;
        MESSAGE( "uplink 200 -> 20 ms, 25% reordered: " << resyncs << " resyncs, " << rewinds << " rewinds, " << probe.Superseded() << " taken back run, misses "
            << probe.Misses( changeAt, net.Now() + 1.0 ) << ", settled " << probe.SettledAfter( changeAt ) << " s after, target " << client.GetClock().GetTargetSlack() );
        CHECK( client.GetState() == NetClient::State::Connected );
        CHECK( resyncs == 1 );
        CHECK( rewinds >= 1u );
        CHECK( probe.Superseded() == 0 );
        CHECK( probe.Misses( changeAt, net.Now() + 1.0 ) == 0 );
        CHECK( probe.SettledAfter( changeAt ) <= 2.0 );
    }
}


TEST_CASE( "Net: reports from before the server heard a resync back neither confirm the commands made again nor steer" )
{
    // A jump back shorter than the round trip: the uplink drops from 400 to 100 ms (18 ticks; the
    // round trip was 25), and from then on loses a quarter of its packets. Reports the server sent
    // before it had the client's first packet from after the jump still arrive after it, holding a
    // newest the client has since made again. They're the old clock's: they don't confirm (the
    // commands made again keep riding every packet, so the loss costs nothing) and don't steer.
    for( uint64_t seed : { 171u, 181u, 191u } )
    {
        CAPTURE( seed );
        Harness net( Link( 20.f, 2.f, 0.f, seed ), Link( 400.f, 2.f, 0.f, seed + 1 ) );
        ClockProbe probe;
        probe.Attach( net );
        net.Start();
        net.Run( 12.0 );
        NetClient& client = *net.ClientNet.GetClient();
        REQUIRE( client.GetState() == NetClient::State::Connected );
        const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
        REQUIRE( remote != nullptr );
        const int resyncsBefore = client.GetClock().GetResyncCount();
        const double changeAt = net.Now();
        net.ClientLink.SetConditions( Link( 100.f, 2.f, 0.25f, seed + 2 ) );
        net.Run( 10.0 );
        const int resyncs = client.GetClock().GetResyncCount() - resyncsBefore;
        const uint64_t misses = probe.Misses( changeAt, net.Now() + 1.0 );
        MESSAGE( "uplink 400 -> 100 ms, then 25% loss: " << resyncs << " resyncs, " << remote->Commands.GetRewindCount() << " rewinds, " << probe.Superseded()
            << " taken back run, misses " << misses << ", target " << client.GetClock().GetTargetSlack() );
        CHECK( client.GetState() == NetClient::State::Connected );
        CHECK( resyncs == 1 );
        CHECK( probe.Superseded() == 0 );
        CHECK( misses == 0 );
    }
}


TEST_CASE( "Net: a client paused on keep-alives for 30 minutes is heard at once when it resumes, and jumps there in one resync" )
{
    // Paused (time scale 0, or the editor's pause), a client makes no ticks and sends a keep-alive
    // every third frame (two ticks' time at 60 fps): 36000 in 30 minutes, past half the 16-bit
    // sequence range. Its first commands after are still the latest packet's, and the report's
    // slack, beyond the +/-64 ticks it's quantized within, goes in whole ticks.
    Harness net( Link( 20.f, 2.f, 0.f, 201 ), Link( 20.f, 2.f, 0.f, 202 ) );
    net.MakeCommand = CommandFor;
    net.Start();
    net.Run( 5.0 );
    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
    REQUIRE( remote != nullptr );
    const uint64_t sentBefore = client.GetConnection().GetStats().PacketsSent;
    const double frame = net.ClientFrameSeconds;
    net.ClientFrameSeconds = 0.0;
    net.Run( 30.0 * 60.0 );
    REQUIRE( client.GetState() == NetClient::State::Connected );
    const uint64_t keepAlives = client.GetConnection().GetStats().PacketsSent - sentBefore;
    const int resyncsBefore = client.GetClock().GetResyncCount();
    net.ClientFrameSeconds = frame;
    net.Run( 1.0 );
    const uint64_t onTimeBefore = remote->Commands.GetOnTimeCount();
    const uint64_t missedBefore = remote->Commands.GetMissedCount();
    net.Run( 5.0 );
    MESSAGE( "paused 30 min (" << keepAlives << " keep-alives): " << client.GetClock().GetResyncCount() - resyncsBefore << " resyncs, then "
        << remote->Commands.GetOnTimeCount() - onTimeBefore << " on time, " << remote->Commands.GetMissedCount() - missedBefore << " missed" );
    CHECK( keepAlives > 33000u );
    CHECK( client.GetState() == NetClient::State::Connected );
    CHECK( client.GetClock().GetResyncCount() - resyncsBefore == 1 );
    CHECK( remote->Commands.GetOnTimeCount() - onTimeBefore >= 290u );
    CHECK( remote->Commands.GetMissedCount() - missedBefore == 0u );
}


TEST_CASE( "Net: a client whose first commands all arrive late is told and catches up" )
{
    // The uplink slows by 150 ms as the server accepts the client: the clock starts from a round
    // trip measured before that, so every command arrives after its tick. Late commands used to
    // leave the server's buffer empty and its reports silent about the client: it stayed behind
    // for good, the server running idle commands for it.
    Harness net( Link( 20.f, 2.f, 0.f, 91 ), Link( 20.f, 2.f, 0.f, 92 ) );
    ClockProbe probe;
    probe.Attach( net );
    net.Start();
    const auto record = net.OnServerTick;
    net.OnServerTick = [&net, record]( NetServer& InServer ) {
        if( InServer.FindClient( 1 ) && net.ClientLink.GetConditions().LatencyMs < 100.f )
        {
            net.ClientLink.SetConditions( Link( 170.f, 2.f, 0.f, 93 ) );
        }
        record( InServer );
    };
    net.Run( 10.0 );
    NetClient& client = *net.ClientNet.GetClient();
    REQUIRE( client.GetState() == NetClient::State::Connected );
    const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
    REQUIRE( remote != nullptr );
    MESSAGE( "late start: " << client.GetClock().GetResyncCount() << " resyncs, " << remote->Commands.GetOnTimeCount() << " on time, " << remote->Commands.GetMissedCount()
        << " missed, " << remote->Commands.GetLateArrivals() << " late, settled " << probe.SettledAfter( 2.0 ) << " s after 2 s" );
    CHECK( client.GetClock().GetResyncCount() == 1 );
    CHECK( remote->Commands.GetOnTimeCount() >= 500 );
    CHECK( probe.Misses( 2.0, net.Now() + 1.0 ) == 0 );
}


TEST_CASE( "Net: the stall check stays quiet on healthy links, at 20 to 240 frames a second and at 128 Hz" )
{
    // A stall is the server waiting well past the client's send interval: a slow client sends
    // a packet every few ticks, which isn't one. At 128 Hz a 60 fps client sends a packet every
    // 2.1 ticks, so a lost one leaves a gap of four: misses the adaptive buffer covers (NET-14).
    struct Case
    {
        int64_t Fps;
        uint16_t TickRate;
    };
    for( const Case c : { Case{ 20, 60 }, Case{ 30, 60 }, Case{ 60, 60 }, Case{ 144, 60 }, Case{ 240, 60 }, Case{ 60, 128 } } )
    {
        for( bool lossy : { false, true } )
        {
            CAPTURE( c.Fps );
            CAPTURE( c.TickRate );
            CAPTURE( lossy );
            const NetConditions link = lossy ? Link( 50.f, 10.f, 0.05f, 0 ) : Link( 20.f, 2.f, 0.f, 0 );
            NetConditions toClient = link;
            NetConditions toServer = link;
            toClient.Seed = 81;
            toServer.Seed = 82;
            Harness net( toClient, toServer );
            net.ClientFrameRate = c.Fps;
            net.ClientFrameSeconds = 1.0 / static_cast<double>( c.Fps );
            net.MakeCommand = CommandFor;
            NetClockSettings clock;
            clock.TickSeconds = 1.0 / c.TickRate;
            net.Start( clock, c.TickRate );
            net.Run( 5.0 );
            NetClient& client = *net.ClientNet.GetClient();
            REQUIRE( client.GetState() == NetClient::State::Connected );
            const NetServer::Client* remote = net.ServerNet.GetServer()->FindClient( client.GetClientId() );
            REQUIRE( remote != nullptr );
            const uint64_t missedBefore = remote->Commands.GetMissedCount();
            const uint64_t onTimeBefore = remote->Commands.GetOnTimeCount();
            const int resyncsBefore = client.GetClock().GetResyncCount();
            const uint64_t stallsBefore = client.GetStalledReports();
            const uint64_t outagesBefore = client.GetOutageCount();
            net.Run( 60.0 );
            const uint64_t missed = remote->Commands.GetMissedCount() - missedBefore;
            const uint64_t consumed = missed + remote->Commands.GetOnTimeCount() - onTimeBefore;
            MESSAGE( c.Fps << " fps, " << c.TickRate << " Hz, " << std::string( lossy ? "50+-10 ms 5% loss" : "20+-2 ms" ) << ": " << client.GetStalledReports() - stallsBefore
                << " stalled reports, " << client.GetOutageCount() - outagesBefore << " outages, " << client.GetClock().GetResyncCount() - resyncsBefore << " resyncs, "
                << missed << " of " << consumed << " missed, target " << client.GetClock().GetTargetSlack() );
            CHECK( consumed >= 60u * c.TickRate - 10u );
            CHECK( client.GetClock().GetResyncCount() == resyncsBefore );
            CHECK( client.GetOutageCount() == outagesBefore );
            // At 20 fps a packet is 3 ticks: two lost in a row (5% loss) is a short stall, a report or
            // two that steer nothing.
            CHECK( client.GetStalledReports() - stallsBefore <= ( lossy ? 10u : 0u ) );
            CHECK( static_cast<double>( missed ) <= consumed * ( lossy ? 0.005 : 0.001 ) );
        }
    }
}


TEST_CASE( "Net: periodic latency spikes cost misses once or twice, then the adaptive buffer covers them (NET-14)" )
{
    // Wi-Fi background scans: every few seconds the radio goes away for 50-150 ms, and what was sent
    // meanwhile arrives at once when it's back. The server waits that long for commands: misses,
    // which grow the target until the buffer covers the next spike. The gap is no outage (it's
    // shorter than MaxSlack plus the usual wait) and its reports steer nothing.
    struct Case
    {
        const char* Name;
        double SpikeMs;
        bool Both;
        NetConditions Link;
    };
    const Case cases[] = {
        { "100 ms up, 20+-2 ms", 100.0, false, Link( 20.f, 2.f, 0.f, 0 ) },
        { "100 ms both ways, 20+-2 ms", 100.0, true, Link( 20.f, 2.f, 0.f, 0 ) },
        { "50 ms up, 20+-2 ms", 50.0, false, Link( 20.f, 2.f, 0.f, 0 ) },
        { "150 ms up, 20+-2 ms", 150.0, false, Link( 20.f, 2.f, 0.f, 0 ) },
        { "100 ms up, 40+-5 ms 2% loss", 100.0, false, Link( 40.f, 5.f, 0.02f, 0 ) },
        { "100 ms both ways, 40+-5 ms 2% loss", 100.0, true, Link( 40.f, 5.f, 0.02f, 0 ) },
    };
    constexpr int kSpikes = 12;
    constexpr double kEvery = 4.0;
    uint64_t seed = 160;
    for( const Case& c : cases )
    {
        const std::string name = c.Name;
        CAPTURE( name );
        NetConditions toClient = c.Link;
        NetConditions toServer = c.Link;
        toClient.Seed = seed;
        toServer.Seed = seed + 1;
        seed += 10;
        Harness net( toClient, toServer );
        ClockProbe probe;
        probe.Attach( net );
        net.Start();
        net.Run( 12.0 );
        NetClient& client = *net.ClientNet.GetClient();
        REQUIRE( client.GetState() == NetClient::State::Connected );
        const int resyncsBefore = client.GetClock().GetResyncCount();
        const uint64_t outagesBefore = client.GetOutageCount();
        const float targetBefore = client.GetClock().GetTargetSlack();
        const double first = net.Now() + 1.0;
        bool spiking = false;
        uint64_t spikeSeed = seed + 1000;
        net.OnMillisecond = [&]( double InNow ) {
            const double phase = InNow - first;
            const int index = phase >= 0.0 ? static_cast<int>( std::floor( phase / kEvery ) ) : -1;
            const double into = phase - index * kEvery;
            if( index >= 0 && index < kSpikes && into < c.SpikeMs / 1000.0 )
            {
                // Held until the spike ends, then on at the link's latency.
                NetConditions held = c.Link;
                held.LatencyMs = c.Link.LatencyMs + static_cast<float>( ( c.SpikeMs / 1000.0 - into ) * 1000.0 );
                held.JitterMs = 0.f;
                held.Seed = ++spikeSeed;
                net.ClientLink.SetConditions( held );
                if( c.Both )
                {
                    held.Seed = ++spikeSeed;
                    net.ServerLink.SetConditions( held );
                }
                spiking = true;
            }
            else if( spiking )
            {
                NetConditions up = c.Link;
                NetConditions down = c.Link;
                up.Seed = ++spikeSeed;
                down.Seed = ++spikeSeed;
                net.ClientLink.SetConditions( up );
                net.ServerLink.SetConditions( down );
                spiking = false;
            }
        };
        net.Run( 1.0 + kSpikes * kEvery );
        std::vector<uint64_t> perSpike;
        std::string list;
        for( int i = 0; i < kSpikes; ++i )
        {
            perSpike.push_back( probe.Misses( first + i * kEvery - 0.5, first + ( i + 1 ) * kEvery - 0.5 ) );
            list += ( i ? " " : "" ) + std::to_string( perSpike.back() );
        }
        uint64_t later = 0;
        uint64_t worstLater = 0;
        for( int i = 2; i < kSpikes; ++i )
        {
            later += perSpike[i];
            worstLater = std::max( worstLater, perSpike[i] );
        }
        MESSAGE( name << ": misses per spike " << list << "; " << client.GetClock().GetResyncCount() - resyncsBefore << " resyncs, " << client.GetOutageCount() - outagesBefore
            << " outages, " << client.GetStallCount() << " stalls, target " << targetBefore << " -> " << client.GetClock().GetTargetSlack() );
        CHECK( client.GetState() == NetClient::State::Connected );
        CHECK( client.GetClock().GetResyncCount() == resyncsBefore );
        CHECK( client.GetOutageCount() == outagesBefore );
        // The link's spikes aren't the client's own hitches (ME-313): their misses grow the target.
        CHECK( client.GetHitchCount() == 0u );
        // The first spike or two cost what the buffer didn't cover; then it covers them.
        CHECK( perSpike[0] > 0u );
        CHECK( worstLater <= 2u );
        CHECK( later <= static_cast<uint64_t>( kSpikes - 2 ) );
    }
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
