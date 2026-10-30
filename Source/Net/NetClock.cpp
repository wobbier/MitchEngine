#include "PCH.h"
#include "Net/NetClock.h"
#include <algorithm>
#include <cmath>


NetClock::NetClock( const NetClockSettings& InSettings )
    : m_settings( InSettings )
    , m_target( InSettings.TargetSlack )
    , m_floor( InSettings.MinSlack )
{
}


uint32_t NetClock::TakeTick()
{
    if( m_settings.AdaptiveSlack )
    {
        // A clean run eases the target down: a quarter tick a second after ShrinkAfterSeconds.
        const uint32_t perSecond = static_cast<uint32_t>( std::max( 1.0, std::round( 1.0 / m_settings.TickSeconds ) ) );
        const uint32_t after = static_cast<uint32_t>( m_settings.ShrinkAfterSeconds * perSecond );
        if( ++m_cleanTicks > after && ( m_cleanTicks - after ) % perSecond == 0 )
        {
            m_target = std::max( m_floor, m_target - m_settings.ShrinkPerSecond );
        }
    }
    return m_tick++;
}


void NetClock::OnMisses( uint32_t InMisses )
{
    if( !m_settings.AdaptiveSlack || InMisses == 0 )
    {
        return;
    }
    m_target = std::min( m_settings.MaxSlack, m_target + static_cast<float>( InMisses ) );
    m_cleanTicks = 0;
}


void NetClock::SetJitter( double InSeconds )
{
    if( !m_settings.AdaptiveSlack )
    {
        return;
    }
    const float jitterTicks = static_cast<float>( std::max( 0.0, InSeconds ) / m_settings.TickSeconds );
    m_floor = std::min( m_settings.MaxSlack, std::max( m_settings.MinSlack, std::ceil( 2.f * jitterTicks ) ) );
    m_target = std::max( m_target, m_floor );
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
    const float error = InSlack - m_target;
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
