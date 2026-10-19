#include "TerrainTextureCache.h"
#include "Dementia.h"
#include "Utils/GUID.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#if USING( ME_PLATFORM_WINDOWS )
#include <Windows.h>
#endif

namespace Moonlight
{
    namespace
    {
        constexpr uint64_t kSeed = 14695981039346656037ull;
        constexpr uint32_t kMagic = 0x4b50544d; // MTPK, little endian.
        constexpr uint32_t kVersion = 1, kPackerRevision = 1; // Bump revision when packing/filter semantics change.
        constexpr size_t kHeaderSize = 64;
        using Bytes = std::vector<uint8_t>;

        void Require( bool condition, const char* message )
        {
            if( !condition ) throw std::runtime_error( message );
        }


        uint64_t Hash( std::span<const uint8_t> bytes, uint64_t seed = kSeed )
        {
            for( auto byte : bytes ) seed = ( seed ^ byte ) * 1099511628211ull;
            return seed;
        }


        void Put( Bytes& bytes, uint64_t value, unsigned count )
        {
            for( unsigned i = 0; i < count; ++i ) bytes.push_back( static_cast<uint8_t>( value >> ( i * 8 ) ) );
        }


        uint64_t Get( const Bytes& bytes, size_t offset, unsigned count )
        {
            uint64_t value = 0;
            for( unsigned i = 0; i < count; ++i ) value |= uint64_t( bytes.at( offset + i ) ) << ( i * 8 );
            return value;
        }


        std::string Hex( uint64_t value )
        {
            std::string result( 16, '0' );
            for( int i = 15; i >= 0; --i ) { result[i] = "0123456789abcdef"[value & 15]; value >>= 4; }
            return result;
        }


        Bytes Identity( const TerrainTexturePaths& paths, TerrainTextureLayout layout, uint16_t cell )
        {
            Bytes result; Put( result, kPackerRevision, 4 ); Put( result, static_cast<uint32_t>( layout ), 4 ); Put( result, cell, 4 );
            // Fingerprint one selected file at a time in bounded chunks; never retain twelve source images.
            std::map<std::filesystem::path, std::pair<uint64_t, uint64_t>> files;
            for( const auto& layer : paths ) for( const auto& path : layer )
            {
                Put( result, !path.empty(), 4 );
                if( path.empty() ) { Put( result, 0, 8 ); Put( result, 0, 8 ); continue; }
                const std::filesystem::path compiled( path + ".dds" );
                const auto selected = std::filesystem::is_regular_file( compiled ) ? compiled : std::filesystem::path( path );
                auto found = files.find( selected );
                if( found == files.end() )
                {
                    std::ifstream file( selected, std::ios::binary | std::ios::ate );
                    if( !file ) throw std::runtime_error( "Cannot read terrain texture: " + selected.string() );
                    const auto size = file.tellg();
                    Require( size > 0 && size <= 128 * 1024 * 1024, "Terrain texture file exceeds 128 MiB" );
                    file.seekg( 0 ); uint64_t hash = kSeed, remaining = static_cast<uint64_t>( size );
                    std::array<uint8_t, 65536> chunk;
                    while( remaining )
                    {
                        const auto count = static_cast<std::streamsize>( std::min<uint64_t>( remaining, chunk.size() ) );
                        Require( bool( file.read( reinterpret_cast<char*>( chunk.data() ), count ) ), "Incomplete terrain texture fingerprint read" );
                        hash = Hash( std::span( chunk ).first( static_cast<size_t>( count ) ), hash ); remaining -= count;
                    }
                    Require( file.peek() == std::char_traits<char>::eof(), "Terrain texture changed during fingerprinting" );
                    found = files.emplace( selected, std::make_pair( static_cast<uint64_t>( size ), hash ) ).first;
                }
                Put( result, found->second.first, 8 ); Put( result, found->second.second, 8 );
            }
            return result;
        }


        TerrainPackedTextures Shape( TerrainTextureLayout layout, uint16_t cell )
        {
            Require( ( layout == TerrainTextureLayout::Array || layout == TerrainTextureLayout::Atlas ) &&
                cell >= 8 && cell <= 512 && !( cell & ( cell - 1 ) ), "Invalid terrain texture packing layout/size" );
            TerrainPackedTextures result; result.Layout = layout; result.CellSize = cell;
            result.AtlasMaxMip = static_cast<uint8_t>( std::bit_width( cell ) - 3 );
            for( auto& map : result.Maps ) for( uint16_t layer = 0; layer < ( layout == TerrainTextureLayout::Array ? 4 : 1 ); ++layer )
                for( unsigned side = cell * ( layout == TerrainTextureLayout::Array ? 1 : 2 ), mip = 0; side; side >>= 1, ++mip )
                    map.push_back( { layer, static_cast<uint16_t>( side ), static_cast<uint16_t>( side ), static_cast<uint8_t>( mip ), {} } );
            return result;
        }


        uint64_t PayloadSize( const TerrainPackedTextures& data )
        {
            uint64_t size = 0;
            for( const auto& map : data.Maps ) for( const auto& upload : map ) size += uint64_t( upload.Width ) * upload.Height * 4;
            return size;
        }


        bool Read( const std::filesystem::path& path, const Bytes& identity, TerrainPackedTextures& data )
        {
            std::error_code ec;
            if( !std::filesystem::exists( path, ec ) && !ec ) return false;
            std::ifstream file( path, std::ios::binary | std::ios::ate );
            Require( bool( file ), "Cannot read terrain texture cache" );
            const auto size = file.tellg(); const auto payloadSize = PayloadSize( data );
            Require( size == static_cast<std::streamoff>( kHeaderSize + identity.size() + payloadSize ), "Invalid terrain texture cache length" );
            Bytes header( kHeaderSize ); file.seekg( 0 );
            Require( bool( file.read( reinterpret_cast<char*>( header.data() ), header.size() ) ), "Truncated terrain texture cache header" );
            Require( Get( header, 0, 4 ) == kMagic && Get( header, 4, 4 ) == kVersion && Get( header, 8, 4 ) == kPackerRevision &&
                Get( header, 12, 4 ) == static_cast<uint32_t>( data.Layout ) && Get( header, 16, 4 ) == data.CellSize && Get( header, 20, 4 ) == data.AtlasMaxMip &&
                Get( header, 24, 4 ) == identity.size() && Get( header, 28, 4 ) == 0 && Get( header, 32, 8 ) == payloadSize &&
                Get( header, 48, 8 ) == Hash( identity ) && Get( header, 56, 8 ) == Hash( std::span( header ).first( 56 ) ), "Invalid terrain texture cache header/version" );
            Bytes stored( identity.size() );
            Require( bool( file.read( reinterpret_cast<char*>( stored.data() ), stored.size() ) ) && stored == identity, "Terrain texture cache input identity mismatch" );
            uint64_t hash = kSeed;
            for( auto& map : data.Maps ) for( auto& upload : map )
            {
                upload.Pixels.resize( size_t( upload.Width ) * upload.Height * 4 );
                Require( bool( file.read( reinterpret_cast<char*>( upload.Pixels.data() ), upload.Pixels.size() ) ), "Truncated terrain texture cache payload" );
                hash = Hash( upload.Pixels, hash );
            }
            Require( hash == Get( header, 40, 8 ) && file.peek() == std::char_traits<char>::eof(), "Terrain texture cache payload checksum failed" );
            return true;
        }


        void Trim( const std::filesystem::path& directory, const std::filesystem::path& keep, uint64_t maximum )
        {
            struct Entry { std::filesystem::path Path; uint64_t Size; std::filesystem::file_time_type Time; };
            std::vector<Entry> entries; uint64_t total = 0;
            for( const auto& file : std::filesystem::directory_iterator( directory ) )
            {
                const auto stem = file.path().stem().string();
                if( file.symlink_status().type() != std::filesystem::file_type::regular || file.path().extension() != ".tpack" || stem.size() != 16 ||
                    stem.find_first_not_of( "0123456789abcdef" ) != std::string::npos ) continue;
                const auto size = file.file_size(); total += size;
                if( file.path() != keep ) entries.push_back( { file.path(), size, file.last_write_time() } );
            }
            std::sort( entries.begin(), entries.end(), []( const auto& a, const auto& b ) { return a.Time != b.Time ? a.Time < b.Time : a.Path < b.Path; } );
            for( const auto& file : entries )
            {
                if( total <= maximum ) break;
                std::error_code ec; if( std::filesystem::remove( file.Path, ec ) ) total -= file.Size;
            }
        }


        void Write( const std::filesystem::path& path, const Bytes& identity, const TerrainPackedTextures& data )
        {
            std::filesystem::path temporary;
            try
            {
                std::filesystem::create_directories( path.parent_path() );
                temporary = path; temporary += "." + std::to_string( GUID::Generate() ) + ".tmp";
                std::ofstream file( temporary, std::ios::binary | std::ios::trunc ); Require( bool( file ), "Cannot stage terrain texture cache" );
                uint64_t hash = kSeed;
                for( const auto& map : data.Maps ) for( const auto& upload : map ) hash = Hash( upload.Pixels, hash );
                Bytes header; Put( header, kMagic, 4 ); Put( header, kVersion, 4 ); Put( header, kPackerRevision, 4 );
                Put( header, static_cast<uint32_t>( data.Layout ), 4 ); Put( header, data.CellSize, 4 ); Put( header, data.AtlasMaxMip, 4 );
                Put( header, identity.size(), 4 ); Put( header, 0, 4 ); Put( header, PayloadSize( data ), 8 ); Put( header, hash, 8 );
                Put( header, Hash( identity ), 8 ); Put( header, Hash( header ), 8 );
                file.write( reinterpret_cast<const char*>( header.data() ), header.size() );
                file.write( reinterpret_cast<const char*>( identity.data() ), identity.size() );
                for( const auto& map : data.Maps ) for( const auto& upload : map )
                    file.write( reinterpret_cast<const char*>( upload.Pixels.data() ), upload.Pixels.size() );
                file.flush(); Require( file.good(), "Cannot write terrain texture cache" ); file.close(); Require( !file.fail(), "Cannot close terrain texture cache" );
#if USING( ME_PLATFORM_WINDOWS )
                Require( MoveFileExW( temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) != 0, "Cannot publish terrain texture cache" );
#else
                std::filesystem::rename( temporary, path );
#endif
            }
            catch( ... )
            {
                if( !temporary.empty() ) { std::error_code ec; std::filesystem::remove( temporary, ec ); }
                throw;
            }
        }
    }


    bool LoadOrPackTerrainTextures( const TerrainTexturePaths& paths, TerrainTextureLayout layout, const TerrainTextureCacheOptions& options,
        TerrainPackedTextures& out, std::string& error, TerrainTextureCacheInfo& info, uint16_t cell )
    {
        info = {};
        if( !options.Enabled || options.Directory.empty() || !options.MaxBytes ) return PackTerrainTextures( paths, layout, out, error, cell );
        try
        {
            auto data = Shape( layout, cell ); const auto stamp = TerrainTextureStamp( paths ); const auto identity = Identity( paths, layout, cell );
            Require( stamp == TerrainTextureStamp( paths ), "Terrain texture changed during fingerprinting" );
            info.File = options.Directory / ( Hex( Hash( identity ) ) + ".tpack" );
            try { info.Hit = Read( info.File, identity, data ); }
            catch( const std::exception& e ) { info.Warning = e.what(); }
            if( !info.Hit )
            {
                if( !PackTerrainTextures( paths, layout, data, error, cell ) ) return false;
                Require( stamp == TerrainTextureStamp( paths ) && identity == Identity( paths, layout, cell ), "Terrain texture changed while preparing its cache entry" );
            }
            Require( stamp == TerrainTextureStamp( paths ), "Terrain texture changed while loading its cache entry" );
            try
            {
                if( !info.Hit && kHeaderSize + identity.size() + PayloadSize( data ) <= options.MaxBytes )
                { Write( info.File, identity, data ); info.Stored = true; }
                if( info.Hit || info.Stored )
                {
                    std::filesystem::last_write_time( info.File, std::filesystem::file_time_type::clock::now() );
                    Trim( options.Directory, info.File, options.MaxBytes );
                }
            }
            catch( const std::exception& e ) { info.Warning = e.what(); }
            out = std::move( data ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }
}
