#include "PCH.h"
#include "Net/NetVersion.h"
#include "Net/BitStream.h"


void NetVersion::Write( BitWriter& InWriter ) const
{
    InWriter.WriteBits( Protocol, 32 );
    for( const uint64_t value : { Build, ScriptSchema, ModSet, Scene } )
    {
        InWriter.WriteBits( static_cast<uint32_t>( value ), 32 );
        InWriter.WriteBits( static_cast<uint32_t>( value >> 32 ), 32 );
    }
}


NetVersion NetVersion::Read( BitReader& InReader )
{
    NetVersion version;
    version.Protocol = InReader.ReadBits( 32 );
    for( uint64_t* value : { &version.Build, &version.ScriptSchema, &version.ModSet, &version.Scene } )
    {
        const uint64_t low = InReader.ReadBits( 32 );
        const uint64_t high = InReader.ReadBits( 32 );
        *value = low | ( high << 32 );
    }
    return version;
}


std::vector<std::string> NetVersion::Differences( const NetVersion& InOther ) const
{
    std::vector<std::string> differences;
    if( Protocol != InOther.Protocol ) differences.push_back( "protocol" );
    if( Build != InOther.Build ) differences.push_back( "build" );
    if( ScriptSchema != InOther.ScriptSchema ) differences.push_back( "scripts" );
    if( ModSet != InOther.ModSet ) differences.push_back( "mods" );
    if( Scene != InOther.Scene ) differences.push_back( "scene" );
    return differences;
}


uint64_t NetVersion::Hash( std::string_view InText, uint64_t InSeed )
{
    uint64_t hash = InSeed;
    for( const char c : InText )
    {
        hash ^= static_cast<uint8_t>( c );
        hash *= 0x100000001B3ull;
    }
    return hash;
}


uint64_t NetVersion::Hash( uint64_t InValue, uint64_t InSeed )
{
    uint64_t hash = InSeed;
    for( int i = 0; i < 8; ++i )
    {
        hash ^= ( InValue >> ( i * 8 ) ) & 0xFF;
        hash *= 0x100000001B3ull;
    }
    return hash;
}
