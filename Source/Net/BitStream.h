#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Bit-packed network serialization (NET-1). Values are written least significant bit first into
// little-endian bytes, so a stream reads the same on every platform. A reader never reads past
// its data: overruns return zero and set IsOverflowed(), and a packet that trips it is dropped.
class BitWriter
{
public:
    // The low InBits (1-32) bits of InValue.
    void WriteBits( uint32_t InValue, int InBits );
    void WriteBool( bool InValue );
    // 7 bits a group with a continue bit: small numbers take a byte.
    void WriteVarUInt( uint64_t InValue );
    // The IEEE bits, unquantized (NetQuantize packs floats with a known range smaller).
    void WriteFloat( float InValue );
    void WriteBytes( const void* InData, size_t InSize );
    // Length (VarUInt) then UTF-8 bytes.
    void WriteString( const std::string& InValue );

    size_t GetBitCount() const { return m_bits; }
    size_t GetByteCount() const { return m_bytes.size(); }
    const std::vector<uint8_t>& GetBytes() const { return m_bytes; }
    void Clear();

private:
    std::vector<uint8_t> m_bytes;
    size_t m_bits = 0;
};


class BitReader
{
public:
    BitReader( const uint8_t* InData, size_t InSize );
    explicit BitReader( const std::vector<uint8_t>& InData );

    uint32_t ReadBits( int InBits );
    bool ReadBool();
    uint64_t ReadVarUInt();
    float ReadFloat();
    // False (and OutData untouched past what fit) on overflow.
    bool ReadBytes( void* OutData, size_t InSize );
    // InMaxLength guards against a hostile length.
    std::string ReadString( size_t InMaxLength = 4096 );

    bool IsOverflowed() const { return m_overflowed; }
    size_t GetBitsRemaining() const { return m_overflowed ? 0 : m_bitCount - m_bit; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_bitCount = 0;
    size_t m_bit = 0;
    bool m_overflowed = false;
};
