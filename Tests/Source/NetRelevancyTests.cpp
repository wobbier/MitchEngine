#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetIdentity.h"
#include "NetHarness.h"
#include <cmath>
#include <map>

namespace NetRelevancyTest
{
    NetConditions Link( float InLatencyMs, uint64_t InSeed )
    {
        NetConditions conditions;
        conditions.LatencyMs = InLatencyMs;
        conditions.JitterMs = 5.f;
        conditions.Loss = 0.02f;
        conditions.Seed = InSeed;
        return conditions;
    }

    struct Bot
    {
        EntityHandle Entity;
        Vector3 Position;
        Vector3 Velocity;
    };

    struct SoakResult
    {
        std::map<uint8_t, uint64_t> Bytes;      // per client, during the soak
        double Seconds = 0.0;
        uint32_t WorstGap = 0;                  // ticks a relevant bot waited for an update
        size_t SentWhileIrrelevant = 0;
        double NearRate = 0.0;                  // updates per tick, bots within 15 m
        double FarRate = 0.0;                   // ... and 35-50 m away
        size_t ClientsConnected = 0;
    };

    // 200 bots wander a 200 m square; 4 clients look on from the corners.
    SoakResult Soak( bool InUseJobs, double InSeconds, bool InMeasure )
    {
        constexpr int kBots = 200;
        constexpr float kRadius = 50.f;
        constexpr uint32_t kBytesPerSecond = 12 * 1024;
        NetHarness::Harness net( Link( 40.f, 61 ), Link( 40.f, 62 ) );
        net.Start();
        for( int i = 0; i < 3; ++i )
        {
            net.AddClient( Link( 40.f, 63 + i ) );
        }
        NetReplicationServer& server = *net.ServerNet.GetReplicationServer();
        server.Settings.RelevantRadius = kRadius;
        server.Settings.BytesPerSecond = kBytesPerSecond;
        server.Settings.UseJobs = InUseJobs;
        const Vector3 viewers[4] = { Vector3( -50.f, 0.f, -50.f ), Vector3( 50.f, 0.f, -50.f ), Vector3( -50.f, 0.f, 50.f ), Vector3( 50.f, 0.f, 50.f ) };
        for( uint8_t id = 1; id <= 4; ++id )
        {
            server.SetViewer( id, viewers[id - 1] );
        }

        uint32_t seed = 12345;
        auto random = [&seed]() { seed = seed * 1664525u + 1013904223u; return static_cast<float>( seed >> 8 ) / 16777216.f * 2.f - 1.f; };
        std::vector<Bot> bots;
        for( int i = 0; i < kBots; ++i )
        {
            Bot bot;
            bot.Entity = net.ServerWorld->CreateEntity();
            bot.Position = Vector3( random() * 100.f, 0.f, random() * 100.f );
            bot.Velocity = Vector3( random() * 3.f, 0.f, random() * 3.f );
            bot.Entity->AddComponent<Transform>().SetPosition( bot.Position );
            bot.Entity->AddComponent<NetIdentity>();
            bots.push_back( bot );
        }

        SoakResult result;
        // Per client and bot: ticks continuously relevant, and the last-sent tick seen.
        std::map<std::pair<uint8_t, uint32_t>, uint32_t> relevantFor;
        std::map<std::pair<uint8_t, uint32_t>, uint32_t> lastSeen;
        uint64_t nearSends = 0, nearTicks = 0, farSends = 0, farTicks = 0;
        bool measuring = false;
        net.OnServerTick = [&]( NetServer& InServer ) {
            // Wander: steer randomly, bounce off the edges.
            for( Bot& bot : bots )
            {
                bot.Velocity = bot.Velocity + Vector3( random() * 0.2f, 0.f, random() * 0.2f );
                const float speed = bot.Velocity.Length();
                if( speed > 4.f )
                {
                    bot.Velocity = bot.Velocity * ( 4.f / speed );
                }
                bot.Position = bot.Position + bot.Velocity * ( 1.f / 60.f );
                for( int axis : { 0, 2 } )
                {
                    if( std::abs( bot.Position[axis] ) > 100.f )
                    {
                        bot.Velocity[axis] = -bot.Velocity[axis];
                    }
                }
                bot.Entity->GetComponent<Transform>().SetPosition( bot.Position );
            }
            if( !measuring )
            {
                return;
            }
            // What the previous tick sent (the server prepares snapshots in EndTick).
            const uint32_t tick = InServer.GetTick() - 1;
            for( uint8_t id = 1; id <= 4; ++id )
            {
                for( const Bot& bot : bots )
                {
                    const uint32_t netId = bot.Entity->GetComponent<NetIdentity>().GetNetId();
                    const auto key = std::make_pair( id, netId );
                    auto captured = server.GetCurrent().Entities.find( netId );
                    if( captured == server.GetCurrent().Entities.end() )
                    {
                        continue;
                    }
                    const float distance = ( captured->second.GetPosition() - viewers[id - 1] ).Length();
                    const uint32_t sent = server.GetLastSentTick( id, netId );
                    const bool fresh = sent == tick && lastSeen[key] != tick;
                    lastSeen[key] = sent;
                    if( fresh && distance > kRadius + 0.01f )
                    {
                        ++result.SentWhileIrrelevant;
                    }
                    if( distance <= kRadius * 0.8f )
                    {
                        const uint32_t since = ++relevantFor[key];
                        if( since > 30 && sent != 0 )
                        {
                            result.WorstGap = std::max( result.WorstGap, tick - sent );
                        }
                    }
                    else
                    {
                        relevantFor[key] = 0;
                    }
                    if( distance < 15.f )
                    {
                        ++nearTicks;
                        nearSends += fresh ? 1 : 0;
                    }
                    else if( distance > 35.f && distance < kRadius )
                    {
                        ++farTicks;
                        farSends += fresh ? 1 : 0;
                    }
                }
            }
        };

        net.Run( 2.0 );
        result.ClientsConnected = net.ServerNet.GetServer()->GetClientCount();
        std::map<uint8_t, uint64_t> before;
        for( uint8_t id = 1; id <= 4; ++id )
        {
            before[id] = server.GetClientSnapshotBytes( id );
        }
        measuring = InMeasure;
        net.Run( InSeconds );
        for( uint8_t id = 1; id <= 4; ++id )
        {
            result.Bytes[id] = server.GetClientSnapshotBytes( id ) - before[id];
        }
        result.Seconds = InSeconds;
        result.NearRate = nearTicks ? static_cast<double>( nearSends ) / nearTicks : 0.0;
        result.FarRate = farTicks ? static_cast<double>( farSends ) / farTicks : 0.0;
        return result;
    }
}

using namespace NetRelevancyTest;
using namespace NetHarness;

TEST_CASE( "Net: a 200-bot soak stays under each client's budget; relevant bots never starve, near ones update most, far ones not at all" )
{
    const SoakResult soak = Soak( true, 20.0, true );
    REQUIRE( soak.ClientsConnected == 4 );
    for( const auto& [id, bytes] : soak.Bytes )
    {
        const double perSecond = bytes / soak.Seconds;
        MESSAGE( "client " << int( id ) << ": " << perSecond << " B/s of snapshots" );
        CHECK( perSecond <= 12 * 1024 * 1.02 );
        CHECK( perSecond > 12 * 1024 * 0.5 );     // it does use what it's given
    }
    MESSAGE( "worst wait " << soak.WorstGap << " ticks; near " << soak.NearRate << " vs far " << soak.FarRate << " updates/tick; "
        << soak.SentWhileIrrelevant << " sent while irrelevant" );
    CHECK( soak.WorstGap <= 30 );
    CHECK( soak.SentWhileIrrelevant == 0 );
    CHECK( soak.NearRate > soak.FarRate * 1.5 );
}

TEST_CASE( "Net: snapshots encoded on jobs are byte-for-byte the serial ones" )
{
    const SoakResult serial = Soak( false, 3.0, false );
    const SoakResult jobs = Soak( true, 3.0, false );
    for( uint8_t id = 1; id <= 4; ++id )
    {
        CHECK( serial.Bytes.at( id ) == jobs.Bytes.at( id ) );
        CHECK( serial.Bytes.at( id ) > 0 );
    }
}
