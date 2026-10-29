#include "PCH.h"
#include "Net/NetClock.h"
#include <algorithm>
#include <cmath>


NetClock::NetClock( const NetClockSettings& InSettings )
    : m_settings( InSettings )
{
}


void NetClock::Start( uint32_t InTick )
{
    m_tick = InTick;
    m_accumulator = 0.0;
    m_dilation = 0.f;
    m_hasResynced = false;
}


int NetClock::Advance( double InRealSeconds )
{
    m_accumulator += InRealSeconds * GetScale();
    const int ticks = static_cast<int>( std::floor( m_accumulator / m_settings.TickSeconds ) );
    m_accumulator -= ticks * m_settings.TickSeconds;
    return ticks;
}


void NetClock::OnSlackReport( float InSlack )
{
    if( m_hasResynced && m_tick - m_resyncedAt < m_settings.ResyncCooldownTicks )
    {
        return;
    }
    const float error = InSlack - m_settings.TargetSlack;
    if( std::abs( error ) > m_settings.ResyncTicks )
    {
        // Too far ahead: step back (the commands for those ticks get sent again); too far behind:
        // skip ahead.
        const int64_t jump = static_cast<int64_t>( std::lround( error ) );
        m_tick = static_cast<uint32_t>( static_cast<int64_t>( m_tick ) - jump );
        m_dilation = 0.f;
        m_hasResynced = true;
        m_resyncedAt = m_tick;
        ++m_resyncs;
        return;
    }
    // Ahead of the target: slow down; behind: speed up.
    m_dilation = std::clamp( -error * m_settings.Gain, -m_settings.MaxDilation, m_settings.MaxDilation );
}
