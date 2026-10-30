#include "PCH.h"
#include "Net/NetVersion.h"
#include "Net/BitStream.h"
#include <algorithm>


void NetVersion::Write( BitWriter& InWriter ) const
{
    InWriter.WriteBits( Protocol, 32 );
    for( const uint64_t value : { Build, ScriptSchema, ModSet, Scene } )
    {
        InWriter.WriteBits( static_cast<uint32_t>( value ), 32 );
        InWriter.WriteBits( static_cast<uint32_t>( value >> 32 ), 32 );
    }
    const size_t count = std::min<size_t>( Mods.size(), 64 );
    InWriter.WriteVarUInt( count );
    for( size_t i = 0; i < count; ++i )
    {
        InWriter.WriteString( Mods[i] );
    }
}


void NetVersion::SetMods( std::vector<std::string> InMods )
{
    std::sort( InMods.begin(), InMods.end() );
    Mods = std::move( InMods );
    ModSet = 0;
    if( !Mods.empty() )
    {
        ModSet = Hash( "" );
        for( const std::string& mod : Mods )
        {
            ModSet = Hash( mod + "\n", ModSet );
        }
    }
}


std::string NetVersion::DescribeModDifferences( const NetVersion& InClient ) const
{
    std::string needed;
    std::string extra;
    for( const std::string& mod : Mods )
    {
        if( !std::binary_search( InClient.Mods.begin(), InClient.Mods.end(), mod ) )
        {
            needed += ( needed.empty() ? "" : ", " ) + mod;
        }
    }
    for( const std::string& mod : InClient.Mods )
    {
        if( !std::binary_search( Mods.begin(), Mods.end(), mod ) )
        {
            extra += ( extra.empty() ? "" : ", " ) + mod;
        }
    }
    std::string text;
    if( !needed.empty() )
    {
        text = "the server needs " + needed;
    }
    if( !extra.empty() )
    {
        text += ( text.empty() ? "" : "; " ) + std::string( "you also have " ) + extra;
    }
    return text;
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
    const uint64_t count = InReader.ReadVarUInt();
    for( uint64_t i = 0; i < count && i < 64 && !InReader.IsOverflowed(); ++i )
    {
        version.Mods.push_back( InReader.ReadString( 256 ) );
    }
    std::sort( version.Mods.begin(), version.Mods.end() );
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
