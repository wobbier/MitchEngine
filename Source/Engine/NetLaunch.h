#pragma once
#include <cstdint>
#include <memory>
#include <string>

class NetCore;
class UdpTransport;
class ConditionedTransport;

// Networking from the command line (NET-5): Engine::Run starts it once the scene has loaded (the
// handshake hashes the scene's networked entities), checks it every frame and ends it on exit.
//
//   --server [port]          host on UDP port (7777 when omitted, 0 = any free port), every interface
//   --connect host:port      join a server (IPv4 address or name)
//   --net-lag ms             hold this side's packets back (one way; both sides: double it for RTT)
//   --net-jitter ms          +/- around the lag
//   --net-loss f             drop this fraction of this side's packets (0.05, or 5%)
//   --net-seed n             the conditions' random seed (repeatable runs)
//   --net-timeout s          seconds of silence before a peer counts as gone (default 10)
//   --net-report file.json   on exit: the session's summary (NetCore::WriteReport)
//   --net-exit-when-empty    server: quit once its last client has gone (smoke tests)
class NetLaunch
{
public:
    NetLaunch();
    ~NetLaunch();

    // True when the command line asks for a session (whether or not it started).
    static bool IsRequested();
    // The command line asked for a session that couldn't start (the process exits non-zero).
    bool HasFailed() const { return !m_error.empty(); }

    // Starts what the command line asks for: false when it asked and couldn't (logged; Update then
    // says to quit, and the report says why). InTickRate: the engine's fixed steps per second.
    bool Start( NetCore& InNet, uint16_t InTickRate );
    // Every frame: logs how the session is going; true when the engine should quit.
    bool Update( NetCore& InNet );
    // Before the engine shuts down: the report, goodbyes, the session ends.
    void Shutdown( NetCore& InNet, double InNow );

private:
    std::unique_ptr<UdpTransport> m_socket;
    std::unique_ptr<ConditionedTransport> m_conditioned;
    std::string m_reportPath;
    std::string m_error;
    bool m_requested = false;
    bool m_exitWhenEmpty = false;
    // What was last logged, so changes log once.
    int m_lastState = -1;
    uint64_t m_lastAccepted = 0;
    uint64_t m_lastDisconnects = 0;
    uint64_t m_lastTimeouts = 0;
    uint64_t m_lastRejected = 0;
};
