#pragma once
#include "Net/NetClock.h"
#include "Net/NetCommands.h"
#include "Net/NetConnection.h"
#include "Net/NetVersion.h"
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class BitReader;
class BitWriter;

// Sessions (NET-2): the handshake, command frames and the clock on top of NetConnection. Neither
// side touches the engine; NetCore drives them from the frame, tests drive them directly.
//
// Handshake (reliable): the client sends Hello with its NetVersion; the server answers Welcome
// (client id, tick rate, its tick) or Reject (what differs). Then, unreliably, every client packet
// carries the commands the server hasn't confirmed (NetCommands), and every server packet the
// tick it simulated, the newest command it has from that client and that client's slack.
namespace NetMessage
{
    enum Type : uint8_t
    {
        Hello = 1,
        Welcome = 2,
        Reject = 3,
        Spawn = 4,
        Despawn = 5,
        Commands = 16,
        ServerState = 17,
        Snapshot = 18,
        SnapshotAck = 19,
        OwnerState = 20,
    };
}


class NetServer
{
public:
    struct Client
    {
        uint8_t Id = 0;
        NetAddress Address = 0;
        std::unique_ptr<NetConnection> Connection;
        NetCommandBuffer Commands;
        InputCommand Current;       // this tick's command (BeginTick)
        bool Accepted = false;
        bool Rejected = false;
        double RejectedAt = 0.0;    // set when the Reject is first sent
        double LastHeard = 0.0;
    };

    NetServer( INetTransport& InTransport, const NetVersion& InVersion, uint16_t InTickRate = 60, size_t InMaxClients = 16 );

    // Packets in: new addresses get a connection; handshakes and commands are handled.
    void Receive( double InNow );
    // The tick about to be simulated: each client's command for it.
    void BeginTick();
    // The tick was simulated: state to every client, one packet each.
    void EndTick( double InNow );

    // Messages beyond the session's own (replication): InReader is past the type byte.
    std::function<void( Client&, uint8_t InType, BitReader& InReader, bool InReliable )> OnMessage;
    // A client was just accepted (a late joiner needs the world sent).
    std::function<void( Client& )> OnClientAccepted;
    // Each accepted client's packet is about to go (EndTick): add what belongs in it.
    std::function<void( Client& )> OnPacket;
    void Send( Client& InClient, const BitWriter& InMessage, bool InReliable );

    uint32_t GetTick() const { return m_tick; }
    uint16_t GetTickRate() const { return m_tickRate; }
    const NetVersion& GetVersion() const { return m_version; }
    // Accepted clients.
    size_t GetClientCount() const;
    const Client* FindClient( uint8_t InId ) const;
    const std::vector<std::unique_ptr<Client>>& GetClients() const { return m_clients; }
    // The reason given to the last client turned away ("" when none was).
    const std::string& GetLastRejection() const { return m_lastRejection; }

private:
    Client* FindOrAdd( NetAddress InAddress );
    void HandleReliable( Client& InClient, const std::vector<uint8_t>& InMessage );
    void HandleUnreliable( Client& InClient, const std::vector<uint8_t>& InMessage );

    INetTransport& m_transport;
    NetVersion m_version;
    uint16_t m_tickRate = 60;
    size_t m_maxClients = 16;
    uint32_t m_tick = 0;
    uint8_t m_nextId = 1;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::string m_lastRejection;
};


class NetClient
{
public:
    enum class State
    {
        Disconnected,
        Connecting,
        Connected,
        Rejected,
    };

    NetClient( INetTransport& InTransport, NetAddress InServer, const NetVersion& InVersion, const NetClockSettings& InClock = NetClockSettings() );

    void Connect();
    void Receive( double InNow );
    // This tick's command (InCommand.Tick is set to the clock's tick, which advances).
    void SubmitCommand( InputCommand InCommand );
    // Sends one packet: the handshake while connecting, then the unconfirmed commands.
    void Flush( double InNow );

    // Messages beyond the session's own (replication): InReader is past the type byte.
    std::function<void( uint8_t InType, BitReader& InReader, bool InReliable )> OnMessage;
    void Send( const BitWriter& InMessage, bool InReliable );

    State GetState() const { return m_state; }
    const std::string& GetRejectReason() const { return m_rejectReason; }
    uint8_t GetClientId() const { return m_clientId; }
    uint16_t GetTickRate() const { return m_tickRate; }
    NetClock& GetClock() { return m_clock; }
    const NetClock& GetClock() const { return m_clock; }
    const NetConnection& GetConnection() const { return m_connection; }
    // The server's latest report.
    float GetServerSlack() const { return m_serverSlack; }
    uint32_t GetServerTick() const { return m_serverTick; }
    size_t GetUnconfirmedCommands() const { return m_commands.size(); }

private:
    void HandleReliable( const std::vector<uint8_t>& InMessage, double InNow );
    void HandleUnreliable( const std::vector<uint8_t>& InMessage );

    INetTransport& m_transport;
    NetConnection m_connection;
    NetVersion m_version;
    NetClock m_clock;
    State m_state = State::Disconnected;
    std::string m_rejectReason;
    uint8_t m_clientId = 0;
    uint16_t m_tickRate = 60;
    std::deque<InputCommand> m_commands;
    bool m_hasReport = false;
    uint32_t m_serverTick = 0;
    float m_serverSlack = 0.f;
};
