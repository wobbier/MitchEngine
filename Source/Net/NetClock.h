#pragma once
#include <cstdint>

struct NetClockSettings
{
    double TickSeconds = 1.0 / 60.0;
    float TargetSlack = 3.f;        // ticks of commands buffered on the server (the starting target)
    // NET-14 (the Overwatch loop): the target grows a tick per miss the server reports and, after
    // ShrinkAfterSeconds without one, eases back by ShrinkPerSecond toward max(MinSlack, 2 x jitter).
    bool AdaptiveSlack = true;
    float MinSlack = 1.f;
    float MaxSlack = 8.f;
    float ShrinkAfterSeconds = 3.f;
    float ShrinkPerSecond = 0.25f;
    float MaxDilation = 0.05f;      // +/- fraction the client's simulation speeds up or slows down
    float Gain = 0.02f;             // dilation per tick of slack error
    float ResyncTicks = 8.f;        // further off than this: jump instead of dilating
    // Behind by more than this (the slack below -BehindTicks: every command arrives after its
    // tick, each one a miss): jump to the target too. Dilating back at 5% takes 20 ticks a tick.
    float BehindTicks = 1.f;
    uint32_t ResyncCooldownTicks = 30;  // reports in flight during a jump describe the old clock
    uint32_t QuietTicks = 10;       // no usable slack report for this long: stop dilating and shrinking (the last one is getting old)
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
    // The tick to stamp the next command with (and, adaptive, a tick of the clean-run count).
    uint32_t TakeTick();

    // The server's smoothed slack for this client, and the slack its newest command arrived with
    // (unsmoothed). The clock is behind only when both say so: the smoothed slack lags a gap's
    // end, and sinks through its first ticks, before the server's wait says it's a stall.
    // InBehindTicks: how far behind jumps (BehindTicks unless the caller knows better: a client
    // back from its own hitch jumps if it's behind at all).
    void OnSlackReport( float InSlack, float InArrived, float InBehindTicks );
    void OnSlackReport( float InSlack, float InArrived ) { OnSlackReport( InSlack, InArrived, m_settings.BehindTicks ); }
    void OnSlackReport( float InSlack ) { OnSlackReport( InSlack, InSlack ); }
    // The server has stopped hearing this client's commands (the uplink is down or slow, or the
    // client itself stalled), so its slack is about the silence, not the clock: hold the clock as
    // it is (no dilation, no resync). Whether the gap's misses grow the target is NetClient's call
    // (OnMisses): a gap a deeper buffer covers, yes; an outage, no.
    void OnStall();
    // Commands the server had to repeat since its last report (adaptive: grows the target).
    // Not during a resync's cooldown: those are the jump's (the clock was off, or still is until
    // it can steer again), not the buffer's.
    void OnMisses( uint32_t InMisses );
    // Within ResyncCooldownTicks of a jump: reports and misses are ignored (those in flight
    // describe the old clock).
    bool InCooldown() const;
    // The link's jitter (round-trip deviation, seconds): the target's floor is twice it in ticks.
    void SetJitter( double InSeconds );
    float GetTargetSlack() const { return m_target; }

    uint32_t GetTick() const { return m_tick; }
    float GetDilation() const { return m_dilation; }
    // How fast simulated time runs against real time (the engine scales its accumulator by it).
    double GetScale() const { return 1.0 + m_dilation; }
    int GetResyncCount() const { return m_resyncs; }
    const NetClockSettings& GetSettings() const { return m_settings; }
    // The server's tick length (its Welcome says).
    void SetTickSeconds( double InSeconds ) { m_settings.TickSeconds = InSeconds; }

private:
    NetClockSettings m_settings;
    uint32_t m_tick = 0;
    double m_accumulator = 0.0;
    float m_dilation = 0.f;
    int m_resyncs = 0;
    bool m_hasResynced = false;
    uint32_t m_resyncedAt = 0;
    float m_target = 3.f;
    float m_floor = 1.f;
    uint32_t m_cleanTicks = 0;
    bool m_heard = false;
    uint32_t m_quietTicks = 0;
};
