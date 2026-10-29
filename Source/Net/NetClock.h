#pragma once
#include <cstdint>

struct NetClockSettings
{
    double TickSeconds = 1.0 / 60.0;
    float TargetSlack = 3.f;        // ticks of commands buffered on the server
    float MaxDilation = 0.05f;      // +/- fraction the client's simulation speeds up or slows down
    float Gain = 0.02f;             // dilation per tick of slack error
    float ResyncTicks = 8.f;        // further off than this: jump instead of dilating
    uint32_t ResyncCooldownTicks = 30;  // reports in flight during a jump describe the old clock
};


// The client's command-frame clock (NET-2, the Overwatch model). The client simulates ahead of
// the server by half a round trip plus a few ticks of slack, so its commands arrive just before
// the server needs them. The server reports the slack it sees; the clock time-dilates (runs its
// ticks up to 5% faster or slower, the tick length unchanged) to hold it at the target, and jumps
// when it's more than ResyncTicks off.
class NetClock
{
public:
    explicit NetClock( const NetClockSettings& InSettings = NetClockSettings() );

    // InTick: the first tick to run.
    void Start( uint32_t InTick );
    // Real time passed: how many ticks are due now (run them, taking each with TakeTick).
    int Advance( double InRealSeconds );
    uint32_t TakeTick() { return m_tick++; }

    // The server's smoothed slack for this client.
    void OnSlackReport( float InSlack );

    uint32_t GetTick() const { return m_tick; }
    float GetDilation() const { return m_dilation; }
    // How fast simulated time runs against real time (the engine scales its accumulator by it).
    double GetScale() const { return 1.0 + m_dilation; }
    int GetResyncCount() const { return m_resyncs; }
    const NetClockSettings& GetSettings() const { return m_settings; }

private:
    NetClockSettings m_settings;
    uint32_t m_tick = 0;
    double m_accumulator = 0.0;
    float m_dilation = 0.f;
    int m_resyncs = 0;
    bool m_hasResynced = false;
    uint32_t m_resyncedAt = 0;
};
