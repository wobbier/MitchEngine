#include "PCH.h"
#include "Net/NetCommands.h"
#include "Net/BitStream.h"
#include "Net/NetQuantize.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kAxisBits = 16;
    constexpr int kAimBits = 16;

    uint32_t QuantizeYawBits( float InDegrees )
    {
        const double wrapped = InDegrees - 360.0 * std::floor( InDegrees / 360.0 );
        return static_cast<uint32_t>( std::llround( wrapped / 360.0 * ( 1 << kAimBits ) ) ) & ( ( 1u << kAimBits ) - 1u );
    }

    float YawFromBits( uint32_t InBits )
    {
        return static_cast<float>( InBits * 360.0 / ( 1 << kAimBits ) );
    }

    uint32_t QuantizePitchBits( float InDegrees )
    {
        return NetQuantize::Quantize( std::clamp( InDegrees, -90.f, 90.f ), -90.f, 90.f, kAimBits );
    }
    // A buffered command this far past the newest one is from a client that lost its mind.
    constexpr uint32_t kMaxAhead = 256;
}


bool InputCommand::operator==( const InputCommand& InOther ) const
{
    return Tick == InOther.Tick && Buttons == InOther.Buttons && Axes == InOther.Axes && ViewTick == InOther.ViewTick
        && HasAim == InOther.HasAim && ( !HasAim || ( Yaw == InOther.Yaw && Pitch == InOther.Pitch ) );
}


float NetCommands::QuantizeYaw( float InDegrees )
{
    return YawFromBits( QuantizeYawBits( InDegrees ) );
}


float NetCommands::QuantizePitch( float InDegrees )
{
    return NetQuantize::Dequantize( QuantizePitchBits( InDegrees ), -90.f, 90.f, kAimBits );
}


double NetCommands::QuantizeViewTick( uint32_t InTick, double InViewTick )
{
    if( InViewTick < 0.0 )
    {
        return -1.0;
    }
    const double behind = std::clamp( static_cast<double>( InTick ) - InViewTick, 0.0, 1023.0 );
    return static_cast<double>( InTick ) - std::round( behind * 64.0 ) / 64.0;
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
        // How far behind its tick the view was, in 1/64 ticks.
        InWriter.WriteBool( command.ViewTick >= 0.0 );
        if( command.ViewTick >= 0.0 )
        {
            const double behind = std::clamp( static_cast<double>( command.Tick ) - command.ViewTick, 0.0, 1023.0 );
            InWriter.WriteBits( static_cast<uint32_t>( std::lround( behind * 64.0 ) ), 16 );
        }
        // Aim (NET-15): present, then changed since the previous command, then yaw and pitch.
        InWriter.WriteBool( command.HasAim );
        if( command.HasAim )
        {
            const uint32_t yaw = QuantizeYawBits( command.Yaw );
            const uint32_t pitch = QuantizePitchBits( command.Pitch );
            const bool changed = i == 0 || !previous.HasAim || yaw != QuantizeYawBits( previous.Yaw ) || pitch != QuantizePitchBits( previous.Pitch );
            InWriter.WriteBool( changed );
            if( changed )
            {
                InWriter.WriteBits( yaw, kAimBits );
                InWriter.WriteBits( pitch, kAimBits );
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
        command.ViewTick = InReader.ReadBool() ? static_cast<double>( command.Tick ) - InReader.ReadBits( 16 ) / 64.0 : -1.0;
        command.HasAim = InReader.ReadBool();
        if( !command.HasAim )
        {
            command.Yaw = command.Pitch = 0.f;
        }
        else if( InReader.ReadBool() )
        {
            command.Yaw = YawFromBits( InReader.ReadBits( kAimBits ) );
            command.Pitch = NetQuantize::Dequantize( InReader.ReadBits( kAimBits ), -90.f, 90.f, kAimBits );
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
        // Its tick ran on a repeat: whatever it pressed that the repeat didn't still happens, a
        // tick or more late, rather than never (the client predicted it).
        auto repeated = m_repeated.find( InCommand.Tick );
        if( repeated != m_repeated.end() )
        {
            const uint32_t pressed = InCommand.Buttons & ~repeated->second;
            if( pressed != 0 )
            {
                m_pendingPresses |= pressed;
                ++m_recoveredPresses;
            }
            m_repeated.erase( repeated );
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
        m_repeated[InTick] = m_last.Buttons;
        if( m_repeated.size() > 64 )
        {
            m_repeated.erase( m_repeated.begin() );
        }
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
    if( m_pendingPresses != 0 )
    {
        // This tick only: the merged press isn't repeated if the next command goes missing too.
        InputCommand merged = m_last;
        merged.Buttons |= m_pendingPresses;
        m_pendingPresses = 0;
        return merged;
    }
    return m_last;
}
