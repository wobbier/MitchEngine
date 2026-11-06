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
// tick it simulated, the newest command it has from that client, that client's slack (in whole
// ticks past +/-64), its misses so far and how long since its last packet of commands arrived
// (protocol 7).
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
        Rpc = 21,           // NET-6: a C# [ServerRpc] / [ClientRpc] call (reliable)
        Owner = 22,         // NET-6: who owns a networked entity now (reliable)
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
        InputCommand Previous;      // the one before (button edges: pressed this tick)
        bool Accepted = false;
        bool Rejected = false;
        double RejectedAt = 0.0;    // set when the Reject is first sent
        double LastHeard = 0.0;
        double AcceptedAt = 0.0;
        bool Left = false;          // said Disconnect: removed on the next Receive
        // When a packet with a new command (one ending on another tick than the last) last arrived
        // (< 0: none yet), and the tick it ended on.
        double CommandsAt = -1.0;
        uint32_t CommandsEnd = 0;
    };

    static constexpr double kDefaultTimeout = 10.0;
    // How long a client's last command keeps repeating while its next ones are missing, before
    // the repeat lets go of buttons and axes (NetCommandBuffer): a quarter second of ticks.
    static constexpr double kDefaultCommandRepeatSeconds = 0.25;
    // The most a server catches up on after a hitch (Engine::Run keeps a server's backlog up to
    // this, and drops the rest; BeginTick's ticks owed are capped to it). Past it the ticks are
    // lost: each client reads its clock as that far ahead and resyncs once. (A server whose steps
    // can't keep up with real time keeps no backlog at all.)
    static constexpr double kMaxCatchUpSeconds = 0.5;

    NetServer( INetTransport& InTransport, const NetVersion& InVersion, uint16_t InTickRate = 60, size_t InMaxClients = 16 );

    // Packets in: new addresses get a connection; handshakes and commands are handled.
    void Receive( double InNow );
    // The tick about to be simulated: each client's command for it. InTicksOwed: ticks still due
    // after it (catching up after a hitch): the slack is measured against where the server will be.
    // Capped at kMaxCatchUpSeconds of ticks.
    void BeginTick( uint32_t InTicksOwed = 0 );
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
    // Missed commands in a row that still repeat a client's last command whole; after that the
    // repeat keeps its aim and ViewTick but is otherwise neutral, until a command arrives in time.
    // Every client, now and later. Default: kDefaultCommandRepeatSeconds at the tick rate.
    void SetMaxCommandRepeats( uint32_t InTicks );
    uint32_t GetMaxCommandRepeats() const { return m_maxCommandRepeats; }

    uint32_t GetTick() const { return m_tick; }
    uint16_t GetTickRate() const { return m_tickRate; }
    // The most ticks owed BeginTick takes into account: kMaxCatchUpSeconds at the tick rate.
    uint32_t GetMaxTicksOwed() const;
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
    void HandleUnreliable( Client& InClient, const std::vector<uint8_t>& InMessage, double InNow );

    INetTransport& m_transport;
    NetVersion m_version;
    uint16_t m_tickRate = 60;
    size_t m_maxClients = 16;
    uint32_t m_tick = 0;
    uint32_t m_ticksOwed = 0;   // at the last BeginTick (a Welcome starts a client's clock past them)
    uint8_t m_nextId = 1;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::string m_lastRejection;
    double m_timeout = kDefaultTimeout;
    uint32_t m_maxCommandRepeats = NetCommandBuffer::kDefaultMaxRepeats;
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
    // Sends one packet: the handshake while connecting, then the unconfirmed commands. About one
    // per tick: when a new command is waiting, else a keep-alive after two ticks without one (one
    // while connecting), so a client drawing far faster than the tick rate doesn't flood the server.
    void Flush( double InNow );
    // Whether Flush( InNow ) would send (what rides along, like a snapshot ack, is only worth queueing then).
    bool IsSendDue( double InNow ) const;
    // Says Disconnect (a few packets' worth) and stops; the server drops this client at once.
    void Disconnect( double InNow );
    // Seconds of silence from the server before giving up (State::TimedOut).
    void SetTimeout( double InSeconds ) { m_timeout = InSeconds; }

    // Messages beyond the session's own (replication): InReader is past the type byte.
    std::function<void( uint8_t InType, BitReader& InReader, bool InReliable )> OnMessage;
    void Send( const BitWriter& InMessage, bool InReliable );

    State GetState() const { return m_state; }
    // The server's words are valid UTF-8 whatever it sent (BitReader::ReadText).
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
    // Stalls: stretches when the server wasn't hearing this client's commands (the uplink was
    // down or slow, or this client hitched), and the reports sent meanwhile. They don't steer the
    // clock.
    uint64_t GetStallCount() const { return m_stalls; }
    uint64_t GetStalledReports() const { return m_stalledReports; }
    // Outages: gaps in what the server heard too long for any buffer to cover (past MaxSlack and
    // the usual wait), whose misses therefore didn't grow the slack target. Shorter gaps' misses
    // do (NET-14).
    uint64_t GetOutageCount() const { return m_outages; }
    // This client's own hitches (ME-313): frames that sent their commands well past the usual
    // interval, a burst of ticks made late. The server's misses on those ticks are the hitch's, not
    // the link's, and don't grow the slack target (a link spike's do).
    uint64_t GetHitchCount() const { return m_hitches; }
    uint64_t GetHitchMisses() const { return m_hitchMisses; }

private:
    struct Report
    {
        uint32_t ServerTick = 0;
        bool HasCommands = false;
        uint32_t Newest = 0;
        float Slack = 0.f;
        uint64_t Misses = 0;
        double Waited = 0.0;    // seconds since the server last heard this client's commands
        // The packet's ack: whether the server had this client's packets up to there.
        bool HasAck = false;
        uint16_t Ack = 0;
    };

    void HandleReliable( const std::vector<uint8_t>& InMessage, double InNow );
    void HandleUnreliable( const std::vector<uint8_t>& InMessage );
    // Every report, in order: confirmations, and its misses sorted into a gap's (held until the
    // gap ends: covered by a deeper buffer or an outage).
    void NoteReport( const Report& InReport );
    // The newest report of a Receive: the clock, and the misses due to grow the target.
    void Steer( const Report& InReport );
    // Of the gap InReport ends, the misses on the hitches' bursts (their own, not the link's).
    uint64_t TakeHitchMisses( const Report& InReport );
    // Whether the server had stopped hearing this client when it sent InReport.
    bool IsStalled( const Report& InReport ) const;
    // How long the server normally waits between this client's commands, at most (seconds).
    double GetUsualWait() const;

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
    double m_lastSentAt = -1.0;
    bool m_newCommand = false;
    bool m_started = false;
    uint8_t m_clientId = 0;
    uint16_t m_tickRate = 60;
    std::deque<InputCommand> m_commands;
    InputCommand m_lastCommand;
    bool m_hasLastCommand = false;
    double m_lastCommandSentAt = -1.0;
    double m_sendInterval = 0.0;    // smoothed seconds between packets with new commands
    // Gaps sampled into m_sendInterval (counted to kHitchAfterSamples): it starts from 0.
    static constexpr int kHitchAfterSamples = 20;
    int m_sendSamples = 0;
    // The link's round-trip deviation before this Receive's packets: a burst of late packets (a
    // latency spike) inflates it, and must not loosen the stall check on the very reports it held.
    double m_jitterBefore = 0.0;
    bool m_hasReport = false;
    bool m_hasNewReport = false;
    Report m_report;
    bool m_hasMissReport = false;
    uint64_t m_lastMisses = 0;
    uint32_t m_serverTick = 0;
    float m_serverSlack = 0.f;
    // While the server waits for this client's next command (a gap), the misses wait in
    // m_gapMisses until it hears it (a gap a deeper buffer covers: they move to m_dueMisses, to
    // grow the target) or the gap outlasts any buffer (an outage: they go). The newest command the
    // server held at the last report: when it moves with a short wait, the server heard the client.
    bool m_hasNewest = false;
    uint32_t m_lastNewest = 0;
    uint64_t m_gapMisses = 0;
    uint64_t m_dueMisses = 0;
    bool m_inOutage = false;
    uint64_t m_outages = 0;
    // The server tick the last report's misses ran to, and the one the gap's run from (its misses
    // are on the ticks after it, up to the newest report's).
    uint32_t m_missesTick = 0;
    uint32_t m_gapFrom = 0;
    // The hitches' bursts the server hasn't heard all of yet, ticks [First, Last]: back to back
    // (stutter) they come faster than the round trip. kMaxHitchBursts at most (the oldest go).
    struct HitchBurst
    {
        uint32_t First = 0;
        uint32_t Last = 0;
    };
    static constexpr size_t kMaxHitchBursts = 8;
    std::vector<HitchBurst> m_hitchBursts;
    // A hitch inside the frame (after its ticks, before its send: Update, Render) made the next
    // frame's first ticks late too, up to m_hitchOpenUntil: they join the newest burst.
    bool m_hitchOpen = false;
    uint32_t m_hitchOpenUntil = 0;
    // m_frameFirstTick: the first tick made since the last packet with commands went out.
    // m_receivedAt: the frame's Receive (before its ticks).
    uint32_t m_frameFirstTick = 0;
    double m_receivedAt = -1.0;
    uint64_t m_hitches = 0;
    uint64_t m_hitchMisses = 0;
    // The server just heard all of a hitch's burst: the next report to steer jumps if behind at all.
    bool m_hitchBack = false;
    bool m_stalled = false;
    uint64_t m_stalls = 0;
    uint64_t m_stalledReports = 0;
    // After a resync, reports describe the old clock until one comes in a packet that acks the
    // first packet with a command made since (m_resyncSequence, set when it goes out).
    bool m_awaitingResync = false;
    bool m_resyncCommandMade = false;
    bool m_hasResyncSequence = false;
    uint16_t m_resyncSequence = 0;
};
