#pragma once
#include "ECS/Core.h"
#include "Net/NetReplication.h"
#include "Net/NetSession.h"
#include <memory>
#include <optional>

// Networking's engine-owned core (NET-2). It runs nothing until Host or Connect starts a session
// over a transport; then the engine's frame drives it:
//
// - Receive( now ) after the frame's events: packets, the handshake, commands, server reports.
// - BeginTick / EndTick around each fixed step: the server takes each client's command for the
//   tick, then reports; the client stamps the local command with its tick.
// - WriteInterpolated( frame seconds ) after the frame's physics poses: the client poses what the
//   server replicates (NET-3), interpolated.
// - Flush( now ) before the frame limiter: the client's one packet of unconfirmed commands.
// - GetClockScale: the client's time dilation; the engine scales its fixed-step accumulator by it,
//   so ticks run up to 5% faster or slower without changing their length.
//
// It never calls GetEngine(): tests run a server world and a client world side by side
// (Tests/Source/NetHarness.h).
class NetCore final
    : public Core<NetCore>
{
public:
    NetCore();
    ~NetCore() override;

    // A session over InTransport, which must outlive it. False when one is already running.
    bool Host( INetTransport& InTransport, uint16_t InTickRate = 60 );
    bool Connect( INetTransport& InTransport, NetAddress InServer, const NetClockSettings& InClock = NetClockSettings() );
    void Shutdown();

    bool IsActive() const { return m_server || m_client; }
    bool IsServer() const { return m_server != nullptr; }
    bool IsClient() const { return m_client != nullptr; }
    NetServer* GetServer() { return m_server.get(); }
    NetClient* GetClient() { return m_client.get(); }
    NetReplicationServer* GetReplicationServer() { return m_replicationServer.get(); }
    NetReplicationClient* GetReplicationClient() { return m_replicationClient.get(); }

    // What this world presents in the handshake: the project's product name and version, the C#
    // script classes, and the networked (NetIdentity) entities placed when the session starts.
    NetVersion MakeVersion();
    // Pins the version instead (tests; a mismatched build).
    void SetVersionOverride( const NetVersion& InVersion ) { m_versionOverride = InVersion; }

    // Frame hooks.
    void Receive( double InNow );
    void BeginTick();
    void EndTick( double InNow );
    void Flush( double InNow );
    // After the frame's physics poses: the client shows mirrored entities interpolated (NET-3).
    void WriteInterpolated( double InFrameSeconds );
    double GetClockScale() const;
    // A server never drops ticks to catch up: every client's commands need simulating.
    bool KeepsBacklog() const { return IsServer(); }

    // The local player's input for the coming ticks (until changed).
    void SetLocalCommand( const InputCommand& InCommand ) { m_localCommand = InCommand; }
    // The tick being simulated (BeginTick to EndTick): the server's, or the client's.
    uint32_t GetTick() const { return m_tick; }

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    std::unique_ptr<NetServer> m_server;
    std::unique_ptr<NetClient> m_client;
    std::unique_ptr<NetReplicationServer> m_replicationServer;
    std::unique_ptr<NetReplicationClient> m_replicationClient;
    std::optional<NetVersion> m_versionOverride;
    InputCommand m_localCommand;
    uint32_t m_tick = 0;
};

ME_REGISTER_CORE( NetCore )
