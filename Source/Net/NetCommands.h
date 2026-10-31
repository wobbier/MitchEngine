#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class BitReader;
class BitWriter;

// One fixed step of a player's input (NET-2): buttons and analog axes for one command frame
// (one engine fixed step). The HK-5 sampler fills these from Networked actions (later); the
// server simulates each client's command for each tick.
struct InputCommand
{
    uint32_t Tick = 0;
    uint32_t Buttons = 0;
    std::array<float, 4> Axes{};    // -1..1, 16 bits on the wire
    // The (fractional) server tick the player was looking at when making it: its interpolated
    // view's render tick (lag compensation, NET-8). < 0: none. 1/64 tick on the wire.
    double ViewTick = -1.0;
    // The player's absolute aim (NET-15), in degrees: Yaw about +Y, 0 looking down +Z and 90 down
    // +X, in [0, 360); Pitch up from level, in [-90, 90]. 16 bits each on the wire (yaw to
    // 0.0055 degrees), sent only when it changes. Absolute, not deltas: the server aims a shot
    // exactly where the client's view did, and a lost command can't drift it.
    bool HasAim = false;
    float Yaw = 0.f;
    float Pitch = 0.f;

    bool operator==( const InputCommand& InOther ) const;
};


namespace NetCommands
{
    // Every client packet carries the commands the server hasn't confirmed, up to this many.
    constexpr size_t kMaxPerPacket = 16;

    // An axis as the server will see it (clients predict with the same value).
    float QuantizeAxis( float InValue );
    // An aim as the server will see it (clients predict with the same value).
    float QuantizeYaw( float InDegrees );
    float QuantizePitch( float InDegrees );
    // A view tick as the server will see it.
    double QuantizeViewTick( uint32_t InTick, double InViewTick );

    // Delta-coded: unchanged buttons / axes and consecutive ticks cost a bit each.
    void Write( BitWriter& InWriter, const InputCommand* InCommands, size_t InCount );
    bool Read( BitReader& InReader, std::vector<InputCommand>& OutCommands );
}


// A client's commands on the server (NET-2), waiting for their tick. Consume hands out the one
// for the tick being simulated, or, when it hasn't arrived, repeats the last one (a miss). Slack
// is how many ticks ahead of the simulation the newest command is when a tick is consumed,
// smoothed; the client's clock steers it to a target. The newest command counts whether it came
// in time or not: a client whose clock is behind reads as behind (negative slack), not as silent.
//
// A client's clock can go back (a resync). Its next packet then ends on a tick older than the
// newest buffered; ReceivePacket drops what's buffered past it (a timeline the client left), and
// the commands it makes for those ticks again replace the old ones, so the server never runs a
// command the client has taken back.
//
// Repeats are capped: past MaxRepeats misses in a row, the repeat keeps its aim and ViewTick but
// lets go of every button and axis, so a client that froze, crashed or lost its link while
// holding move and fire stops moving and firing until its next command arrives (which resets
// the run). Within the cap, a repeat is the last command exactly.
class NetCommandBuffer
{
public:
    // 0.25 s at 60 Hz. NetServer sets it from its own tick rate (SetMaxCommandRepeats).
    static constexpr uint32_t kDefaultMaxRepeats = 15;

    // InNextTick: the next tick the server will simulate; older commands are too late. A copy of
    // a tick already buffered is ignored.
    void Receive( const InputCommand& InCommand, uint32_t InNextTick );
    // One packet's commands, oldest first. InLatest: no packet sent after it has been seen (it's
    // the client's latest word): its commands replace buffered copies, and if it ends on an older
    // tick than the newest buffered, the client's clock went back and what's past it is dropped.
    // An older packet (reordered) only fills gaps up to the newest tick.
    void ReceivePacket( const InputCommand* InCommands, size_t InCount, uint32_t InNextTick, bool InLatest );
    // Before the first command arrives, and for ticks before the first one's (the client's clock
    // starts ahead), an idle command that isn't a miss. InTicksOwed: ticks the server will run
    // straight after this one (catching up after a hitch): the slack is measured against the
    // last of them, since the client's clock kept time while the server didn't.
    InputCommand Consume( uint32_t InTick, uint32_t InTicksOwed = 0 );

    // Misses in a row that still repeat the last command's buttons and axes; later ones are
    // neutral (aim and ViewTick kept). 0: neutral from the first miss.
    void SetMaxRepeats( uint32_t InTicks ) { m_maxRepeats = InTicks; }
    uint32_t GetMaxRepeats() const { return m_maxRepeats; }
    // Misses since the last command that arrived in time (0 while commands are on time).
    uint32_t GetRepeatRun() const { return m_repeatRun; }
    // Ticks that ran on a neutral repeat, past the cap.
    uint64_t GetNeutralCount() const { return m_neutral; }

    bool HasReceived() const { return m_hasReceived; }
    uint32_t GetNewestTick() const { return m_newest; }
    float GetSlack() const { return m_slack; }
    float GetLastSlackSample() const { return m_lastSample; }
    uint64_t GetOnTimeCount() const { return m_onTime; }
    uint64_t GetMissedCount() const { return m_missed; }
    uint64_t GetLateArrivals() const { return m_lateArrivals; }
    // Presses that came in a command after its tick had been repeated, merged into a later tick
    // instead of being lost (NET-14).
    uint64_t GetRecoveredPresses() const { return m_recoveredPresses; }
    // Times the client's clock went back and the commands past its new newest were dropped.
    uint64_t GetRewindCount() const { return m_rewinds; }
    size_t GetBuffered() const { return m_commands.size(); }

private:
    void Accept( const InputCommand& InCommand, uint32_t InNextTick, bool InReplace );

    std::map<uint32_t, InputCommand> m_commands;
    InputCommand m_last;    // the last command that arrived in time (its Tick moved on by repeats)
    // Missed ticks -> the buttons the client last sent (what the repeat used, or, past the cap,
    // would have): a button held through a gap isn't a new press.
    std::map<uint32_t, uint32_t> m_repeated;
    uint32_t m_pendingPresses = 0;
    uint64_t m_recoveredPresses = 0;
    bool m_hasReceived = false;
    bool m_hasSlack = false;
    uint32_t m_first = 0;   // the earliest tick received: the client's clock started there
    uint32_t m_newest = 0;
    uint64_t m_rewinds = 0;
    float m_slack = 0.f;
    float m_lastSample = 0.f;
    uint64_t m_onTime = 0;
    uint64_t m_missed = 0;
    uint64_t m_lateArrivals = 0;
    uint32_t m_maxRepeats = kDefaultMaxRepeats;
    uint32_t m_repeatRun = 0;
    uint64_t m_neutral = 0;
};
