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
//
// Ending (NET-5): either side says Disconnect (unreliable, sent a few times) when it leaves; a peer
// that goes quiet for the timeout (10 s by default) is dropped instead and counted as timed out.
namespace NetMessage
{
    enum Type : uint8_t
    {
        Hello = 1,
        Welcome = 2,
        Reject = 3,
        Spawn = 4,
        Despawn = 5,
        Disconnect = 6,
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
        double AcceptedAt = 0.0;
        bool Left = false;          // said Disconnect: removed on the next Receive
    };

    static constexpr double kDefaultTimeout = 10.0;

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
    // An accepted client is about to go (it left or timed out).
    std::function<void( Client& )> OnClientRemoved;
    // Each accepted client's packet is about to go (EndTick): add what belongs in it.
    std::function<void( Client& )> OnPacket;
    void Send( Client& InClient, const BitWriter& InMessage, bool InReliable );

    // Tells every client the server is going (Disconnect, a few packets' worth) and drops them.
    void Close( double InNow, const std::string& InReason = "server closed" );
    // Seconds of silence before an accepted client is dropped as timed out.
    void SetTimeout( double InSeconds ) { m_timeout = InSeconds; }

    uint32_t GetTick() const { return m_tick; }
    uint16_t GetTickRate() const { return m_tickRate; }
    const NetVersion& GetVersion() const { return m_version; }
    // Accepted clients.
    size_t GetClientCount() const;
    const Client* FindClient( uint8_t InId ) const;
    const std::vector<std::unique_ptr<Client>>& GetClients() const { return m_clients; }
    // The reason given to the last client turned away ("" when none was).
    const std::string& GetLastRejection() const { return m_lastRejection; }
    // Since the server started: clients accepted, clients that said Disconnect, clients that went
    // quiet past the timeout, and clients turned away.
    uint64_t GetAcceptedCount() const { return m_acceptedCount; }
    uint64_t GetDisconnectCount() const { return m_disconnectCount; }
    uint64_t GetTimeoutCount() const { return m_timeoutCount; }
    uint64_t GetRejectedCount() const { return m_rejectedCount; }

private:
    Client* FindOrAdd( NetAddress InAddress );
    // A client id no accepted client holds (1-255, moving on so a freed id isn't reused at once); 0 when none is free.
    uint8_t AllocateId();
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
    double m_timeout = kDefaultTimeout;
    uint64_t m_acceptedCount = 0;
    uint64_t m_disconnectCount = 0;
    uint64_t m_timeoutCount = 0;
    uint64_t m_rejectedCount = 0;
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
        TimedOut,   // no word from the server for the timeout (while connecting, too)
    };

    NetClient( INetTransport& InTransport, NetAddress InServer, const NetVersion& InVersion, const NetClockSettings& InClock = NetClockSettings() );

    // Starts the handshake. One session per NetClient: to reconnect, make a new one.
    void Connect();
    void Receive( double InNow );
    // This tick's command (InCommand.Tick is set to the clock's tick, which advances).
    void SubmitCommand( InputCommand InCommand );
    // Sends one packet: the handshake while connecting, then the unconfirmed commands.
    void Flush( double InNow );
    // Says Disconnect (a few packets' worth) and stops; the server drops this client at once.
    void Disconnect( double InNow );
    // Seconds of silence from the server before giving up (State::TimedOut).
    void SetTimeout( double InSeconds ) { m_timeout = InSeconds; }

    // Messages beyond the session's own (replication): InReader is past the type byte.
    std::function<void( uint8_t InType, BitReader& InReader, bool InReliable )> OnMessage;
    void Send( const BitWriter& InMessage, bool InReliable );

    State GetState() const { return m_state; }
    const std::string& GetRejectReason() const { return m_rejectReason; }
    // Why a connected session ended: "left" (Disconnect), or what the server said when it closed.
    const std::string& GetDisconnectReason() const { return m_disconnectReason; }
    // When the Welcome arrived (the InNow clock); negative until connected.
    double GetConnectedAt() const { return m_connectedAt; }
    // When a connected session ended (left, closed, timed out); negative while it runs.
    double GetEndedAt() const { return m_endedAt; }
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
    std::string m_disconnectReason;
    double m_timeout = NetServer::kDefaultTimeout;
    double m_lastHeard = -1.0;      // set when the Hello first goes out, then by every packet
    double m_connectedAt = -1.0;
    double m_endedAt = -1.0;
    bool m_started = false;
    uint8_t m_clientId = 0;
    uint16_t m_tickRate = 60;
    std::deque<InputCommand> m_commands;
    bool m_hasReport = false;
    uint32_t m_serverTick = 0;
    float m_serverSlack = 0.f;
};
