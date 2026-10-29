#include "PCH.h"
#include "Net/NetCommands.h"
#include "Net/BitStream.h"
#include "Net/NetQuantize.h"
#include <cmath>

namespace
{
    constexpr int kAxisBits = 16;
    // A buffered command this far past the newest one is from a client that lost its mind.
    constexpr uint32_t kMaxAhead = 256;
}


bool InputCommand::operator==( const InputCommand& InOther ) const
{
    return Tick == InOther.Tick && Buttons == InOther.Buttons && Axes == InOther.Axes;
}


float NetCommands::QuantizeAxis( float InValue )
{
    return NetQuantize::Dequantize( NetQuantize::Quantize( InValue, -1.f, 1.f, kAxisBits ), -1.f, 1.f, kAxisBits );
}


void NetCommands::Write( BitWriter& InWriter, const InputCommand* InCommands, size_t InCount )
{
    const size_t count = InCount < kMaxPerPacket ? InCount : kMaxPerPacket;
    InWriter.WriteBits( static_cast<uint32_t>( count ), 5 );
    InputCommand previous;
    for( size_t i = 0; i < count; ++i )
    {
        const InputCommand& command = InCommands[i];
        const bool consecutive = i > 0 && command.Tick == previous.Tick + 1;
        InWriter.WriteBool( consecutive );
        if( !consecutive )
        {
            InWriter.WriteBits( command.Tick, 32 );
        }
        const bool buttonsChanged = i == 0 || command.Buttons != previous.Buttons;
        InWriter.WriteBool( buttonsChanged );
        if( buttonsChanged )
        {
            InWriter.WriteBits( command.Buttons, 32 );
        }
        for( size_t axis = 0; axis < command.Axes.size(); ++axis )
        {
            const uint32_t value = NetQuantize::Quantize( command.Axes[axis], -1.f, 1.f, kAxisBits );
            const bool changed = i == 0 || value != NetQuantize::Quantize( previous.Axes[axis], -1.f, 1.f, kAxisBits );
            InWriter.WriteBool( changed );
            if( changed )
            {
                InWriter.WriteBits( value, kAxisBits );
            }
        }
        previous = command;
    }
}


bool NetCommands::Read( BitReader& InReader, std::vector<InputCommand>& OutCommands )
{
    const uint32_t count = InReader.ReadBits( 5 );
    if( count > kMaxPerPacket )
    {
        return false;
    }
    InputCommand previous;
    for( uint32_t i = 0; i < count; ++i )
    {
        InputCommand command = previous;
        command.Tick = InReader.ReadBool() && i > 0 ? previous.Tick + 1 : InReader.ReadBits( 32 );
        if( InReader.ReadBool() )
        {
            command.Buttons = InReader.ReadBits( 32 );
        }
        for( float& axis : command.Axes )
        {
            if( InReader.ReadBool() )
            {
                axis = NetQuantize::Dequantize( InReader.ReadBits( kAxisBits ), -1.f, 1.f, kAxisBits );
            }
            else
            {
                axis = NetCommands::QuantizeAxis( axis );
            }
        }
        OutCommands.push_back( command );
        previous = command;
    }
    return !InReader.IsOverflowed();
}


void NetCommandBuffer::Receive( const InputCommand& InCommand, uint32_t InNextTick )
{
    if( InCommand.Tick < InNextTick )
    {
        // Redundant copies of commands already simulated are expected; a first copy this late
        // means a miss already happened.
        if( !m_hasReceived || InCommand.Tick > m_newest )
        {
            ++m_lateArrivals;
        }
        return;
    }
    if( m_hasReceived && InCommand.Tick > m_newest + kMaxAhead )
    {
        return;
    }
    m_commands.emplace( InCommand.Tick, InCommand );
    if( !m_hasReceived || InCommand.Tick > m_newest )
    {
        m_newest = InCommand.Tick;
    }
    m_hasReceived = true;
}


InputCommand NetCommandBuffer::Consume( uint32_t InTick )
{
    if( !m_hasReceived )
    {
        InputCommand idle;
        idle.Tick = InTick;
        return idle;
    }
    auto it = m_commands.find( InTick );
    if( it != m_commands.end() )
    {
        m_last = it->second;
        ++m_onTime;
    }
    else
    {
        // Missing: keep doing what they were doing (never stall the server).
        m_last.Tick = InTick;
        ++m_missed;
    }
    m_commands.erase( m_commands.begin(), m_commands.upper_bound( InTick ) );

    m_lastSample = static_cast<float>( static_cast<int64_t>( m_newest ) - static_cast<int64_t>( InTick ) );
    // Smoothed; a jump (the client resynced) is taken at once.
    if( !m_hasSlack || std::abs( m_lastSample - m_slack ) > 4.f )
    {
        m_slack = m_lastSample;
        m_hasSlack = true;
    }
    else
    {
        m_slack += ( m_lastSample - m_slack ) * 0.1f;
    }
    return m_last;
}
