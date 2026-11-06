#include "PCH.h"
#include <doctest/doctest.h>
#include "Engine/ScriptReloadGate.h"
#include "NetHarness.h"

using namespace NetHarness;

// ME-311: a .cs edit doesn't hot reload while the process's network session is live.

namespace ScriptReloadGateTest
{
    NetConditions Clean( uint64_t InSeed )
    {
        NetConditions conditions;
        conditions.LatencyMs = 20.f;
        conditions.Seed = InSeed;
        return conditions;
    }

    // What Engine::PollAssetChanges does each frame: the gate hears whether the session is live,
    // then the frame's .cs changes. Returns the reload requests the frame makes.
    int Frame( ScriptReloadGate& InGate, const NetCore& InNet, int InEdits = 0 )
    {
        int requests = InGate.Update( InNet.IsSessionLive() ) ? 1 : 0;
        for( int i = 0; i < InEdits; ++i )
        {
            requests += InGate.OnScriptChanged() ? 1 : 0;
        }
        return requests;
    }
}


TEST_CASE( "Scripts: hot reload pauses for a live session, holds edits made during it, and rebuilds them once when it ends (ME-311)" )
{
    ScriptReloadGate gate;
    // No session (the editor, offline play): every edit reloads.
    CHECK( gate.Update( false ) == false );
    CHECK( !gate.IsPaused() );
    CHECK( gate.OnScriptChanged() );
    CHECK( gate.OnScriptChanged() );
    CHECK( !gate.HasPendingReload() );

    // A session starts: edits are held, however many.
    CHECK( gate.Update( true ) == false );
    CHECK( gate.IsPaused() );
    CHECK( gate.Update( true ) == false );
    CHECK( !gate.OnScriptChanged() );
    CHECK( !gate.OnScriptChanged() );
    CHECK( gate.HasPendingReload() );
    CHECK( gate.Update( true ) == false );

    // It ends: one rebuild for everything held, then edits reload again.
    CHECK( gate.Update( false ) == true );
    CHECK( !gate.IsPaused() );
    CHECK( !gate.HasPendingReload() );
    CHECK( gate.Update( false ) == false );
    CHECK( gate.OnScriptChanged() );

    // A session with no edits ends without a rebuild.
    CHECK( gate.Update( true ) == false );
    CHECK( gate.Update( false ) == false );
}


TEST_CASE( "Net: a session is live from Host / Connect until Close or its end, and the script reload gate follows it (ME-311)" )
{
    Harness net( ScriptReloadGateTest::Clean( 1 ), ScriptReloadGateTest::Clean( 2 ) );
    ScriptReloadGate serverGate;
    ScriptReloadGate clientGate;
    CHECK( !net.ServerNet.IsSessionLive() );
    CHECK( !net.ClientNet.IsSessionLive() );
    CHECK( ScriptReloadGateTest::Frame( serverGate, net.ServerNet, 1 ) == 1 );
    CHECK( ScriptReloadGateTest::Frame( clientGate, net.ClientNet, 1 ) == 1 );

    // Hosting, and connecting: live before the handshake finishes.
    net.Start();
    CHECK( net.ServerNet.IsSessionLive() );
    CHECK( net.ClientNet.IsSessionLive() );
    CHECK( ScriptReloadGateTest::Frame( serverGate, net.ServerNet, 2 ) == 0 );
    CHECK( ScriptReloadGateTest::Frame( clientGate, net.ClientNet ) == 0 );
    net.Run( 1.0 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    CHECK( net.ServerNet.IsSessionLive() );
    CHECK( net.ClientNet.IsSessionLive() );
    CHECK( ScriptReloadGateTest::Frame( serverGate, net.ServerNet, 1 ) == 0 );
    CHECK( ScriptReloadGateTest::Frame( clientGate, net.ClientNet, 1 ) == 0 );

    // The client leaves: its session is over (still readable until Shutdown), the server's isn't.
    net.ClientNet.Close( net.Now() );
    net.Run( 0.5 );
    CHECK( net.ClientNet.IsActive() );
    CHECK( !net.ClientNet.IsSessionLive() );
    CHECK( net.ServerNet.IsSessionLive() );
    CHECK( ScriptReloadGateTest::Frame( clientGate, net.ClientNet ) == 1 );   // the held edit rebuilds
    CHECK( ScriptReloadGateTest::Frame( clientGate, net.ClientNet, 1 ) == 1 );
    CHECK( ScriptReloadGateTest::Frame( serverGate, net.ServerNet, 1 ) == 0 );

    // The server closes: over, and it rebuilds what was edited meanwhile.
    net.ServerNet.Close( net.Now() );
    CHECK( net.ServerNet.IsActive() );
    CHECK( !net.ServerNet.IsSessionLive() );
    CHECK( ScriptReloadGateTest::Frame( serverGate, net.ServerNet ) == 1 );
    net.ServerNet.Shutdown();
    net.ClientNet.Shutdown();
    CHECK( !net.ServerNet.IsSessionLive() );
    CHECK( !net.ClientNet.IsSessionLive() );

    // Hosting again after a Close: live again.
    net.ServerNet.Host( net.ServerLink );
    CHECK( net.ServerNet.IsSessionLive() );
    CHECK( ScriptReloadGateTest::Frame( serverGate, net.ServerNet, 1 ) == 0 );
}


TEST_CASE( "Net: a client turned away for its scripts' schema isn't in a session, so its hot reload resumes (ME-311)" )
{
    Harness net( ScriptReloadGateTest::Clean( 3 ), ScriptReloadGateTest::Clean( 4 ) );
    NetVersion client = net.ClientNet.MakeVersion();
    client.ScriptSchema ^= 0x5157;   // a peer that reloaded scripts the server doesn't have
    net.ClientNet.SetVersionOverride( client );
    ScriptReloadGate gate;
    net.Start();
    CHECK( ScriptReloadGateTest::Frame( gate, net.ClientNet, 1 ) == 0 );
    net.Run( 1.0 );
    REQUIRE( net.ClientNet.GetClient()->GetState() == NetClient::State::Rejected );
    CHECK( !net.ClientNet.IsSessionLive() );
    CHECK( net.ServerNet.IsSessionLive() );
    CHECK( ScriptReloadGateTest::Frame( gate, net.ClientNet ) == 1 );
}
