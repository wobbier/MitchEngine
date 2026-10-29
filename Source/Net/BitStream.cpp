#include "PCH.h"
#include "Net/BitStream.h"
#include <algorithm>
#include <cstring>


void BitWriter::WriteBits( uint32_t InValue, int InBits )
{
    int remaining = std::clamp( InBits, 0, 32 );
    uint64_t value = InBits >= 32 ? InValue : ( InValue & ( ( 1u << remaining ) - 1u ) );
    while( remaining > 0 )
    {
        const size_t byte = m_bits >> 3;
        const int offset = static_cast<int>( m_bits & 7 );
        if( byte == m_bytes.size() )
        {
            m_bytes.push_back( 0 );
        }
        const int count = std::min( 8 - offset, remaining );
        m_bytes[byte] |= static_cast<uint8_t>( ( value & ( ( 1u << count ) - 1u ) ) << offset );
        value >>= count;
        m_bits += static_cast<size_t>( count );
        remaining -= count;
    }
}


void BitWriter::WriteBool( bool InValue )
{
    WriteBits( InValue ? 1u : 0u, 1 );
}


void BitWriter::WriteVarUInt( uint64_t InValue )
{
    do
    {
        const uint32_t group = static_cast<uint32_t>( InValue & 0x7F );
        InValue >>= 7;
        WriteBits( group | ( InValue != 0 ? 0x80u : 0u ), 8 );
    } while( InValue != 0 );
}


void BitWriter::WriteFloat( float InValue )
{
    uint32_t bits = 0;
    std::memcpy( &bits, &InValue, sizeof( bits ) );
    WriteBits( bits, 32 );
}


void BitWriter::WriteBytes( const void* InData, size_t InSize )
{
    const uint8_t* bytes = static_cast<const uint8_t*>( InData );
    for( size_t i = 0; i < InSize; ++i )
    {
        WriteBits( bytes[i], 8 );
    }
}


void BitWriter::WriteString( const std::string& InValue )
{
    WriteVarUInt( InValue.size() );
    WriteBytes( InValue.data(), InValue.size() );
}


void BitWriter::Clear()
{
    m_bytes.clear();
    m_bits = 0;
}


BitReader::BitReader( const uint8_t* InData, size_t InSize )
    : m_data( InData )
    , m_bitCount( InSize * 8 )
{
}


BitReader::BitReader( const std::vector<uint8_t>& InData )
    : BitReader( InData.data(), InData.size() )
{
}


uint32_t BitReader::ReadBits( int InBits )
{
    const int bits = std::clamp( InBits, 0, 32 );
    if( m_overflowed || m_bit + static_cast<size_t>( bits ) > m_bitCount )
    {
        m_overflowed = true;
        return 0;
    }
    uint64_t value = 0;
    int read = 0;
    while( read < bits )
    {
        const size_t byte = m_bit >> 3;
        const int offset = static_cast<int>( m_bit & 7 );
        const int count = std::min( 8 - offset, bits - read );
        const uint64_t chunk = ( m_data[byte] >> offset ) & ( ( 1u << count ) - 1u );
        value |= chunk << read;
        read += count;
        m_bit += static_cast<size_t>( count );
    }
    return static_cast<uint32_t>( value );
}


bool BitReader::ReadBool()
{
    return ReadBits( 1 ) != 0;
}


uint64_t BitReader::ReadVarUInt()
{
    uint64_t value = 0;
    for( int shift = 0; shift < 64; shift += 7 )
    {
        const uint32_t group = ReadBits( 8 );
        value |= static_cast<uint64_t>( group & 0x7F ) << shift;
        if( ( group & 0x80 ) == 0 || m_overflowed )
        {
            return m_overflowed ? 0 : value;
        }
    }
    // More than ten groups: not something a writer makes.
    m_overflowed = true;
    return 0;
}


float BitReader::ReadFloat()
{
    const uint32_t bits = ReadBits( 32 );
    float value = 0.f;
    std::memcpy( &value, &bits, sizeof( value ) );
    return value;
}


bool BitReader::ReadBytes( void* OutData, size_t InSize )
{
    if( m_overflowed || m_bit + InSize * 8 > m_bitCount )
    {
        m_overflowed = true;
        return false;
    }
    uint8_t* bytes = static_cast<uint8_t*>( OutData );
    for( size_t i = 0; i < InSize; ++i )
    {
        bytes[i] = static_cast<uint8_t>( ReadBits( 8 ) );
    }
    return true;
}


std::string BitReader::ReadString( size_t InMaxLength )
{
    const uint64_t length = ReadVarUInt();
    if( m_overflowed || length > InMaxLength || length * 8 > GetBitsRemaining() )
    {
        m_overflowed = true;
        return {};
    }
    std::string value( static_cast<size_t>( length ), '\0' );
    ReadBytes( value.data(), value.size() );
    return value;
}
