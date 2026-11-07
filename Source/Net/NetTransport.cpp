#include "PCH.h"
#include "Net/NetTransport.h"
#include <algorithm>


void LoopbackNetwork::Register( LoopbackTransport& InTransport )
{
    m_transports[InTransport.GetAddress()] = &InTransport;
}


void LoopbackNetwork::Unregister( LoopbackTransport& InTransport )
{
    auto it = m_transports.find( InTransport.GetAddress() );
    if( it != m_transports.end() && it->second == &InTransport )
    {
        m_transports.erase( it );
    }
}


bool LoopbackNetwork::Deliver( NetAddress InFrom, NetAddress InTo, const uint8_t* InData, size_t InSize )
{
    auto it = m_transports.find( InTo );
    if( it == m_transports.end() )
    {
        return false;
    }
    NetPacket packet;
    packet.From = InFrom;
    packet.Data.assign( InData, InData + InSize );
    it->second->m_inbox.push_back( std::move( packet ) );
    return true;
}


LoopbackTransport::LoopbackTransport( LoopbackNetwork& InNetwork, NetAddress InAddress )
    : m_network( InNetwork )
    , m_address( InAddress )
{
    m_network.Register( *this );
}


LoopbackTransport::~LoopbackTransport()
{
    m_network.Unregister( *this );
}


void LoopbackTransport::Send( NetAddress InTo, const uint8_t* InData, size_t InSize )
{
    m_network.Deliver( m_address, InTo, InData, InSize );
}


bool LoopbackTransport::Receive( NetPacket& OutPacket )
{
    if( m_inbox.empty() )
    {
        return false;
    }
    OutPacket = std::move( m_inbox.front() );
    m_inbox.pop_front();
    return true;
}


ConditionedTransport::ConditionedTransport( INetTransport& InInner, const NetConditions& InConditions )
    : m_inner( InInner )
{
    SetConditions( InConditions );
}


uint64_t ConditionedTransport::SeedOf( const NetConditions& InConditions )
{
    return InConditions.Seed != 0 ? InConditions.Seed : 0x9E3779B97F4A7C15ull;
}


bool ConditionedTransport::IsActive( const NetConditions& InConditions )
{
    return InConditions.LatencyMs > 0.f || InConditions.JitterMs > 0.f || InConditions.Loss > 0.f || InConditions.Duplicate > 0.f || InConditions.Reorder > 0.f;
}


void ConditionedTransport::SetConditions( const NetConditions& InConditions, bool InReseed )
{
    m_conditions = InConditions;
    if( InReseed )
    {
        m_rng = SeedOf( InConditions );
    }
}


void ConditionedTransport::SetInboundConditions( const NetConditions& InConditions, bool InReseed )
{
    m_inbound = InConditions;
    m_inboundOn = IsActive( InConditions );
    if( InReseed )
    {
        // Apart from the sends' sequence, so the same seed both ways doesn't drop in step.
        m_inboundRng = SeedOf( InConditions ) ^ 0xD6E8FEB86659FD93ull;
    }
    if( !m_inboundOn )
    {
        // Off: what was held is due now (Receive hands it out first, in arrival order).
        for( Held& held : m_inboundHeld )
        {
            held.Due = m_now;
        }
    }
}


double ConditionedTransport::NextUnit( uint64_t& InOutRng )
{
    // xorshift64*: the same sequence on every platform (std distributions aren't).
    InOutRng ^= InOutRng >> 12;
    InOutRng ^= InOutRng << 25;
    InOutRng ^= InOutRng >> 27;
    const uint64_t value = InOutRng * 0x2545F4914F6CDD1Dull;
    return static_cast<double>( value >> 11 ) * ( 1.0 / 9007199254740992.0 );
}


void ConditionedTransport::Hold( std::vector<Held>& InOutHeld, const NetConditions& InConditions, uint64_t& InOutRng, NetAddress InAddress, const uint8_t* InData, size_t InSize )
{
    double delayMs = InConditions.LatencyMs + ( NextUnit( InOutRng ) * 2.0 - 1.0 ) * InConditions.JitterMs;
    if( NextUnit( InOutRng ) < InConditions.Reorder )
    {
        delayMs += InConditions.ReorderMs;
    }
    Held held;
    held.Due = m_now + std::max( delayMs, 0.0 ) / 1000.0;
    held.Order = m_order++;
    held.To = InAddress;
    held.Data.assign( InData, InData + InSize );
    InOutHeld.push_back( std::move( held ) );
}


void ConditionedTransport::Send( NetAddress InTo, const uint8_t* InData, size_t InSize )
{
    if( NextUnit( m_rng ) < m_conditions.Loss )
    {
        ++m_dropped;
        return;
    }
    Hold( m_held, m_conditions, m_rng, InTo, InData, InSize );
    if( NextUnit( m_rng ) < m_conditions.Duplicate )
    {
        ++m_duplicated;
        Hold( m_held, m_conditions, m_rng, InTo, InData, InSize );
    }
}


bool ConditionedTransport::Receive( NetPacket& OutPacket )
{
    if( m_inboundOn )
    {
        // Everything that has arrived goes into the hold, conditioned.
        NetPacket packet;
        while( m_inner.Receive( packet ) )
        {
            if( NextUnit( m_inboundRng ) < m_inbound.Loss )
            {
                ++m_inboundDropped;
                continue;
            }
            Hold( m_inboundHeld, m_inbound, m_inboundRng, packet.From, packet.Data.data(), packet.Data.size() );
            if( NextUnit( m_inboundRng ) < m_inbound.Duplicate )
            {
                ++m_inboundDuplicated;
                Hold( m_inboundHeld, m_inbound, m_inboundRng, packet.From, packet.Data.data(), packet.Data.size() );
            }
        }
    }
    // The earliest due (ties in arrival order); held ones go first once conditioning is turned off.
    size_t best = m_inboundHeld.size();
    for( size_t i = 0; i < m_inboundHeld.size(); ++i )
    {
        const Held& held = m_inboundHeld[i];
        if( held.Due <= m_now && ( best == m_inboundHeld.size() || held.Due < m_inboundHeld[best].Due
            || ( held.Due == m_inboundHeld[best].Due && held.Order < m_inboundHeld[best].Order ) ) )
        {
            best = i;
        }
    }
    if( best < m_inboundHeld.size() )
    {
        OutPacket.From = m_inboundHeld[best].To;
        OutPacket.Data = std::move( m_inboundHeld[best].Data );
        m_inboundHeld.erase( m_inboundHeld.begin() + static_cast<std::ptrdiff_t>( best ) );
        return true;
    }
    return !m_inboundOn && m_inner.Receive( OutPacket );
}


void ConditionedTransport::Update( double InNow )
{
    m_now = InNow;
    // Release what's due, earliest first (ties in send order).
    std::sort( m_held.begin(), m_held.end(), []( const Held& InA, const Held& InB ) {
        return InA.Due != InB.Due ? InA.Due < InB.Due : InA.Order < InB.Order;
    } );
    size_t released = 0;
    while( released < m_held.size() && m_held[released].Due <= InNow )
    {
        const Held& held = m_held[released];
        m_inner.Send( held.To, held.Data.data(), held.Data.size() );
        ++released;
    }
    m_held.erase( m_held.begin(), m_held.begin() + static_cast<std::ptrdiff_t>( released ) );
    m_inner.Update( InNow );
}
