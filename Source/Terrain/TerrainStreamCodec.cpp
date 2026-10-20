#include "PCH.h"
#include "TerrainStreamCodec.h"
#include <algorithm>
#include <stdexcept>

namespace TerrainData
{
    namespace
    {
        void Require( bool condition, const char* message ) { if( !condition ) throw std::runtime_error( message ); }


        void U32( std::vector<uint8_t>& out, uint32_t value )
        {
            for( unsigned shift = 0; shift < 32; shift += 8 ) out.push_back( uint8_t( value >> shift ) );
        }


        uint32_t U32( std::span<const uint8_t> bytes )
        {
            uint32_t value = 0; for( unsigned i = 0; i < 4; ++i ) value |= uint32_t( bytes[i] ) << ( i * 8 ); return value;
        }
    }


    bool EncodeStreamBlock( std::span<const uint8_t> bytes, std::vector<uint8_t>& out, std::string& error )
    {
        try
        {
            Require( bytes.size() <= MaxStreamBlockBytes && bytes.size() % 4 == 0, "Invalid terrain compression input size" );
            uint32_t codec = 0; std::vector<uint8_t> packed;
            // Canonical records contain four floats; coverage also contains scalar metadata
            // and RGBA8 weights. Choose the smallest of raw and 4/8/16-byte plane layouts.
            for( uint32_t candidate = 1; candidate <= 3; ++candidate )
            {
                const size_t stride = size_t( 1 ) << ( candidate + 1 );
                if( bytes.empty() || bytes.size() % stride ) continue;
                const size_t words = bytes.size() / stride;
                const auto at = [&]( size_t i ) { return bytes[( i % words ) * stride + i / words]; };
                const auto run = [&]( size_t i ) {
                    size_t count = 1; while( count < 130 && i + count < bytes.size() && at( i + count ) == at( i ) ) ++count; return count;
                };
                std::vector<uint8_t> trial; trial.reserve( bytes.size() );
                for( size_t i = 0; i < bytes.size(); )
                {
                    const size_t count = run( i );
                    if( count >= 3 ) { trial.push_back( uint8_t( 128 + count - 3 ) ); trial.push_back( at( i ) ); i += count; }
                    else
                    {
                        const size_t begin = i++;
                        while( i - begin < 128 && i < bytes.size() && run( i ) < 3 ) ++i;
                        trial.push_back( uint8_t( i - begin - 1 ) );
                        for( size_t j = begin; j < i; ++j ) trial.push_back( at( j ) );
                    }
                }
                if( trial.size() < ( codec ? packed.size() : bytes.size() ) ) { packed = std::move( trial ); codec = candidate; }
            }
            const bool compressed = codec != 0;
            std::vector<uint8_t> result; result.reserve( 8 + ( compressed ? packed.size() : bytes.size() ) );
            U32( result, codec ); U32( result, uint32_t( bytes.size() ) );
            if( compressed ) result.insert( result.end(), packed.begin(), packed.end() );
            else result.insert( result.end(), bytes.begin(), bytes.end() );
            out = std::move( result ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool DecodeStreamBlock( std::span<const uint8_t> bytes, uint64_t expected, std::vector<uint8_t>& out, std::string& error )
    {
        try
        {
            Require( expected <= MaxStreamBlockBytes && expected % 4 == 0 && bytes.size() >= 8 && bytes.size() <= expected + 8, "Invalid terrain compressed block size" );
            const uint32_t codec = U32( bytes.first( 4 ) ), decoded = U32( bytes.subspan( 4, 4 ) );
            Require( decoded == expected && codec <= 3, "Terrain block codec or decoded size is invalid" );
            std::vector<uint8_t> result( size_t( expected ), uint8_t( 0 ) );
            if( codec == 0 )
            {
                Require( bytes.size() == expected + 8, "Truncated raw terrain block" );
                std::copy( bytes.begin() + 8, bytes.end(), result.begin() );
            }
            else
            {
                Require( bytes.size() - 8 < expected, "Terrain compressed block must be smaller than its raw payload" );
                const size_t stride = size_t( 1 ) << ( codec + 1 );
                Require( expected % stride == 0, "Invalid terrain byte-plane layout" );
                const size_t words = size_t( expected / stride ); size_t output = 0, input = 8;
                const auto put = [&]( uint8_t value ) { result[( output % words ) * stride + output / words] = value; ++output; };
                while( input < bytes.size() )
                {
                    const uint8_t control = bytes[input++];
                    const size_t count = control < 128 ? size_t( control ) + 1 : size_t( control & 127 ) + 3;
                    Require( count <= expected - output, "Terrain compressed run exceeds its decoded block" );
                    if( control < 128 )
                    {
                        Require( count <= bytes.size() - input, "Truncated terrain compressed literal" );
                        for( size_t i = 0; i < count; ++i ) put( bytes[input++] );
                    }
                    else
                    {
                        Require( input < bytes.size(), "Truncated terrain compressed run" );
                        const uint8_t value = bytes[input++]; for( size_t i = 0; i < count; ++i ) put( value );
                    }
                }
                Require( output == expected, "Truncated decoded terrain block" );
            }
            out = std::move( result ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }
}
