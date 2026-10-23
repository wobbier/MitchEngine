#pragma once
#include <cstdint>

// Deterministic hashing and random numbers for procedural content. Unlike <random>'s distributions,
// every result here is specified bit for bit, so a seed produces the same terrain, tree or scatter
// on every platform and standard library.
namespace Hash
{
    constexpr uint64_t SplitMix64( uint64_t InValue )
    {
        uint64_t z = InValue + 0x9E3779B97F4A7C15ULL;
        z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
        z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
        return z ^ ( z >> 31 );
    }

    // Order-dependent: Combine( a, b ) != Combine( b, a ).
    constexpr uint64_t Combine( uint64_t InSeed, uint64_t InValue )
    {
        return SplitMix64( InSeed ^ ( InValue + 0x9E3779B97F4A7C15ULL + ( InSeed << 6 ) + ( InSeed >> 2 ) ) );
    }

    template<typename... Values>
    constexpr uint64_t Of( uint64_t InSeed, Values... InValues )
    {
        uint64_t result = SplitMix64( InSeed );
        ( ( result = Combine( result, static_cast<uint64_t>( InValues ) ) ), ... );
        return result;
    }

    // 32-bit lattice hashes for noise (PCG output permutation over a mixed coordinate).
    constexpr uint32_t Mix32( uint32_t InValue )
    {
        const uint32_t state = InValue * 747796405u + 2891336453u;
        const uint32_t word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
        return ( word >> 22u ) ^ word;
    }

    constexpr uint32_t Lattice( int32_t InX, int32_t InY, uint32_t InSeed )
    {
        return Mix32( static_cast<uint32_t>( InX ) * 0x8DA6B343u ^ Mix32( static_cast<uint32_t>( InY ) * 0xD8163841u ^ InSeed ) );
    }

    constexpr uint32_t Lattice( int32_t InX, int32_t InY, int32_t InZ, uint32_t InSeed )
    {
        return Mix32( static_cast<uint32_t>( InX ) * 0x8DA6B343u ^ Mix32( static_cast<uint32_t>( InY ) * 0xD8163841u ^ Mix32( static_cast<uint32_t>( InZ ) * 0xCB1AB31Fu ^ InSeed ) ) );
    }

    // [0, 1) with 24 bits of precision (exactly representable as a float).
    constexpr float ToUnit( uint32_t InValue )
    {
        return static_cast<float>( InValue >> 8 ) * ( 1.f / 16777216.f );
    }

    constexpr float ToUnit( uint64_t InValue )
    {
        return ToUnit( static_cast<uint32_t>( InValue >> 32 ) );
    }
}

// PCG32 (O'Neill, XSH-RR). Small, fast and identical everywhere.
class Pcg32
{
public:
    explicit constexpr Pcg32( uint64_t InSeed, uint64_t InStream = 0xDA3E39CB94B95BDBULL )
        : m_increment( ( InStream << 1u ) | 1u )
    {
        Next();
        m_state += InSeed;
        Next();
    }

    constexpr uint32_t Next()
    {
        const uint64_t old = m_state;
        m_state = old * 6364136223846793005ULL + m_increment;
        const uint32_t shifted = static_cast<uint32_t>( ( ( old >> 18u ) ^ old ) >> 27u );
        const uint32_t rotation = static_cast<uint32_t>( old >> 59u );
        return ( shifted >> rotation ) | ( shifted << ( ( 0u - rotation ) & 31u ) );
    }

    // [0, 1).
    constexpr float Unit() { return Hash::ToUnit( Next() ); }
    // [InMin, InMax).
    constexpr float Range( float InMin, float InMax ) { return InMin + ( InMax - InMin ) * Unit(); }
    // [-1, 1).
    constexpr float Signed() { return Unit() * 2.f - 1.f; }
    // Base value scaled by 1 +- InVariance.
    constexpr float Vary( float InBase, float InVariance ) { return InBase * ( 1.f + InVariance * Signed() ); }
    constexpr bool Chance( float InProbability ) { return Unit() < InProbability; }
    // [0, InCount), without modulo bias for practical counts (Lemire's multiply-shift).
    constexpr uint32_t Below( uint32_t InCount ) { return static_cast<uint32_t>( ( static_cast<uint64_t>( Next() ) * InCount ) >> 32 ); }

private:
    uint64_t m_state = 0;
    uint64_t m_increment = 1;
};
