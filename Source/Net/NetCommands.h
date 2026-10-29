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

    bool operator==( const InputCommand& InOther ) const;
};


namespace NetCommands
{
    // Every client packet carries the commands the server hasn't confirmed, up to this many.
    constexpr size_t kMaxPerPacket = 16;

    // An axis as the server will see it (clients predict with the same value).
    float QuantizeAxis( float InValue );
    // A view tick as the server will see it.
    double QuantizeViewTick( uint32_t InTick, double InViewTick );

    // Delta-coded: unchanged buttons / axes and consecutive ticks cost a bit each.
    void Write( BitWriter& InWriter, const InputCommand* InCommands, size_t InCount );
    bool Read( BitReader& InReader, std::vector<InputCommand>& OutCommands );
}


// A client's commands on the server (NET-2), waiting for their tick. Consume hands out the one
// for the tick being simulated, or, when it hasn't arrived, repeats the last one (a miss). Slack
// is how many ticks ahead of the simulation the newest command is when a tick is consumed,
// smoothed; the client's clock steers it to a target.
class NetCommandBuffer
{
public:
    // InNextTick: the next tick the server will simulate; older commands are too late.
    void Receive( const InputCommand& InCommand, uint32_t InNextTick );
    InputCommand Consume( uint32_t InTick );

    bool HasReceived() const { return m_hasReceived; }
    uint32_t GetNewestTick() const { return m_newest; }
    float GetSlack() const { return m_slack; }
    float GetLastSlackSample() const { return m_lastSample; }
    uint64_t GetOnTimeCount() const { return m_onTime; }
    uint64_t GetMissedCount() const { return m_missed; }
    uint64_t GetLateArrivals() const { return m_lateArrivals; }
    size_t GetBuffered() const { return m_commands.size(); }

private:
    std::map<uint32_t, InputCommand> m_commands;
    InputCommand m_last;
    bool m_hasReceived = false;
    bool m_hasSlack = false;
    uint32_t m_newest = 0;
    float m_slack = 0.f;
    float m_lastSample = 0.f;
    uint64_t m_onTime = 0;
    uint64_t m_missed = 0;
    uint64_t m_lateArrivals = 0;
};
