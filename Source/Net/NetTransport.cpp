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


void ConditionedTransport::SetConditions( const NetConditions& InConditions )
{
    m_conditions = InConditions;
    m_rng = InConditions.Seed != 0 ? InConditions.Seed : 0x9E3779B97F4A7C15ull;
}


double ConditionedTransport::NextUnit()
{
    // xorshift64*: the same sequence on every platform (std distributions aren't).
    m_rng ^= m_rng >> 12;
    m_rng ^= m_rng << 25;
    m_rng ^= m_rng >> 27;
    const uint64_t value = m_rng * 0x2545F4914F6CDD1Dull;
    return static_cast<double>( value >> 11 ) * ( 1.0 / 9007199254740992.0 );
}


void ConditionedTransport::Hold( NetAddress InTo, const uint8_t* InData, size_t InSize )
{
    double delayMs = m_conditions.LatencyMs + ( NextUnit() * 2.0 - 1.0 ) * m_conditions.JitterMs;
    if( NextUnit() < m_conditions.Reorder )
    {
        delayMs += m_conditions.ReorderMs;
    }
    Held held;
    held.Due = m_now + std::max( delayMs, 0.0 ) / 1000.0;
    held.Order = m_order++;
    held.To = InTo;
    held.Data.assign( InData, InData + InSize );
    m_held.push_back( std::move( held ) );
}


void ConditionedTransport::Send( NetAddress InTo, const uint8_t* InData, size_t InSize )
{
    if( NextUnit() < m_conditions.Loss )
    {
        ++m_dropped;
        return;
    }
    Hold( InTo, InData, InSize );
    if( NextUnit() < m_conditions.Duplicate )
    {
        ++m_duplicated;
        Hold( InTo, InData, InSize );
    }
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
