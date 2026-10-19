#include "PCH.h"
#include "TerrainRecovery.h"
#include "TerrainSurface.h"
#include "JSON.h"
#include "Utils/GUID.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#if USING( ME_PLATFORM_WINDOWS )
#include <Windows.h>
#endif

namespace TerrainData
{
    namespace
    {
        constexpr size_t kHeaderSize = 36;
        constexpr uint64_t kMaxMetadata = 8 * 1024 * 1024, kMaxPayload = 128 * 1024 * 1024;
        uint64_t Hash( std::span<const uint8_t> bytes )
        {
            uint64_t hash = 14695981039346656037ull;
            for( uint8_t byte : bytes ) { hash ^= byte; hash *= 1099511628211ull; }
            return hash;
        }
        void Put( std::vector<uint8_t>& bytes, uint64_t value, int count )
        {
            for( int i = 0; i < count; ++i ) bytes.push_back( static_cast<uint8_t>( value >> ( 8 * i ) ) );
        }
        uint64_t Get( std::span<const uint8_t> bytes, size_t offset, int count )
        {
            uint64_t value = 0;
            for( int i = 0; i < count; ++i ) value |= uint64_t( bytes[offset + i] ) << ( 8 * i );
            return value;
        }
        bool SameSettings( const Settings& a, const Settings& b )
        {
            return a.TileQuads == b.TileQuads && a.PatchQuads == b.PatchQuads && a.SampleSpacing == b.SampleSpacing &&
                a.HeightBase == b.HeightBase && a.HeightRange == b.HeightRange && a.MinTile == b.MinTile && a.TilesX == b.TilesX && a.TilesZ == b.TilesZ;
        }
        bool ValidateInfo( const RecoveryInfo& info, Manifest& base, std::string& error )
        {
            if( !info.AssetGUID || info.AssetPath.empty() || info.AssetPath.size() > 16384 || info.AssetPath.find( '\0' ) != std::string::npos ||
                std::filesystem::path( info.AssetPath ).extension() != ".terrain" || info.BaseManifest.size() > kMaxMetadata )
            { error = "Invalid terrain recovery identity or source path"; return false; }
            return DeserializeManifest( info.BaseManifest, base, error );
        }
        bool Header( std::span<const uint8_t> bytes, uint64_t total, uint32_t& metadataSize, uint64_t& payloadSize, std::string& error )
        {
            if( bytes.size() < kHeaderSize || bytes[0] != 'M' || bytes[1] != 'T' || bytes[2] != 'R' || bytes[3] != 'C' || Get( bytes, 4, 4 ) != 1 )
            { error = "Invalid terrain recovery header or version"; return false; }
            metadataSize = static_cast<uint32_t>( Get( bytes, 8, 4 ) ); payloadSize = Get( bytes, 12, 8 );
            if( !metadataSize || metadataSize > kMaxMetadata || !payloadSize || payloadSize > kMaxPayload || total != kHeaderSize + metadataSize + payloadSize )
            { error = "Invalid terrain recovery lengths"; return false; }
            return true;
        }
        bool Metadata( std::span<const uint8_t> bytes, RecoveryInfo& out, std::string& error )
        {
            const auto size = static_cast<size_t>( Get( bytes, 8, 4 ) );
            const auto metadata = bytes.subspan( kHeaderSize, size );
            if( Hash( metadata ) != Get( bytes, 20, 8 ) ) { error = "Terrain recovery metadata checksum mismatch"; return false; }
            const auto value = json::parse( metadata.begin(), metadata.end() );
            if( value.at( "Format" ) != "MitchTerrainRecovery" || ( value.at( "Version" ).get<int>() != 1 && value.at( "Version" ).get<int>() != 2 ) )
            { error = "Unsupported terrain recovery metadata version"; return false; }
            RecoveryInfo info; info.AssetPath = value.at( "AssetPath" ).get<std::string>();
            const auto guid = value.at( "AssetGUID" ).get<std::string>();
            if( guid.size() != 16 || guid.find_first_not_of( "0123456789abcdefABCDEF" ) != std::string::npos )
            { error = "Invalid terrain recovery GUID"; return false; }
            info.AssetGUID = std::stoull( guid, nullptr, 16 );
            if( !value.at( "SavedAt" ).is_number_unsigned() && ( !value.at( "SavedAt" ).is_number_integer() || value.at( "SavedAt" ).get<int64_t>() < 0 ) )
            { error = "Invalid terrain recovery timestamp"; return false; }
            info.SavedAt = value.at( "SavedAt" ).get<uint64_t>(); info.BaseManifest = value.at( "BaseManifest" ).get<std::string>();
            Manifest base;
            if( !ValidateInfo( info, base, error ) ) return false;
            out = std::move( info ); return true;
        }
    }

    bool SerializeRecovery( const RecoveryInfo& info, const Heightfield& field, std::vector<uint8_t>& out, std::string& error )
    {
        try
        {
            Manifest base;
            if( !ValidateInfo( info, base, error ) ) return false;
            if( !SameSettings( base.Config, field.GetSettings() ) ) { error = "Recovery source settings differ from the base manifest"; return false; }
            const uint64_t perTile = field.GetSettings().SamplesPerTile() * sizeof( float ) + 40;
            if( field.GetTiles().size() > ( kMaxPayload - 88 ) / perTile ) { error = "Terrain recovery exceeds the 128 MiB payload budget"; return false; }
            std::ostringstream guid; guid << std::hex << std::setw( 16 ) << std::setfill( '0' ) << info.AssetGUID;
            json value; value["Format"] = "MitchTerrainRecovery"; value["Version"] = 1;
            value["AssetPath"] = info.AssetPath; value["AssetGUID"] = guid.str(); value["SavedAt"] = info.SavedAt; value["BaseManifest"] = info.BaseManifest;
            std::vector<uint8_t> payload;
            Heightfield heights = field;
            if( !heights.SetSurface( nullptr, error ) || !CookHeightfield( heights, info.AssetGUID, HeightEncoding::Float32, payload, error ) ) return false;
            if( field.GetSurface() )
            {
                std::vector<uint8_t> surface;
                if( !SerializeSurface( *field.GetSurface(), field.GetSettings(), surface, error ) ) return false;
                value["Version"] = 2; value["HeightBytes"] = payload.size();
                payload.insert( payload.end(), surface.begin(), surface.end() );
            }
            const std::string metadata = value.dump();
            if( metadata.size() > kMaxMetadata ) { error = "Terrain recovery metadata exceeds its budget"; return false; }

            if( payload.size() > kMaxPayload ) { error = "Terrain recovery exceeds the 128 MiB payload budget"; return false; }
            std::vector<uint8_t> bytes{ 'M', 'T', 'R', 'C' };
            Put( bytes, 1, 4 ); Put( bytes, metadata.size(), 4 ); Put( bytes, payload.size(), 8 );
            Put( bytes, Hash( { reinterpret_cast<const uint8_t*>( metadata.data() ), metadata.size() } ), 8 ); Put( bytes, Hash( payload ), 8 );
            bytes.insert( bytes.end(), metadata.begin(), metadata.end() ); bytes.insert( bytes.end(), payload.begin(), payload.end() );
            out = std::move( bytes ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }

    bool DeserializeRecovery( std::span<const uint8_t> bytes, RecoveryInfo& outInfo, Heightfield& outField, std::string& error )
    {
        try
        {
            uint32_t metadataSize; uint64_t payloadSize;
            if( !Header( bytes, bytes.size(), metadataSize, payloadSize, error ) ) return false;
            RecoveryInfo info;
            if( !Metadata( bytes, info, error ) ) return false;
            const auto payload = bytes.subspan( kHeaderSize + metadataSize, static_cast<size_t>( payloadSize ) );
            if( Hash( payload ) != Get( bytes, 28, 8 ) ) { error = "Terrain recovery payload checksum mismatch"; return false; }
            Heightfield field; CookInfo cook;
            const auto metadata = json::parse( bytes.begin() + kHeaderSize, bytes.begin() + kHeaderSize + metadataSize );
            const bool surfaceVersion = metadata.at( "Version" ).get<int>() == 2;
            const auto& heightBytes = surfaceVersion ? metadata.at( "HeightBytes" ) : json( payload.size() );
            if( !heightBytes.is_number_unsigned() ) { error = "Invalid recovery height length"; return false; }
            const uint64_t heightSize = heightBytes.get<uint64_t>();
            if( !heightSize || heightSize > payload.size() || ( surfaceVersion && heightSize == payload.size() ) )
            { error = "Invalid recovery height/surface boundary"; return false; }
            if( !ReadCookedHeightfield( payload.first( static_cast<size_t>( heightSize ) ), field, cook, error ) ) return false;
            if( surfaceVersion )
            {
                Surface surface;
                if( !DeserializeSurface( payload.subspan( static_cast<size_t>( heightSize ) ), field.GetSettings(), surface, error ) ||
                    !field.SetSurface( std::make_shared<Surface>( std::move( surface ) ), error ) ) return false;
            }
            Manifest base;
            if( !ValidateInfo( info, base, error ) ) return false;
            if( cook.AssetGUID != info.AssetGUID || cook.Encoding != HeightEncoding::Float32 || !SameSettings( base.Config, field.GetSettings() ) )
            { error = "Terrain recovery payload does not match its source identity/settings"; return false; }
            outInfo = std::move( info ); outField = std::move( field ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }

    bool InspectRecovery( const std::filesystem::path& path, RecoveryInfo& out, std::string& error )
    {
        try
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate );
            if( !file ) { error = "Could not open terrain recovery"; return false; }
            const auto size = file.tellg();
            std::vector<uint8_t> prefix( kHeaderSize ); file.seekg( 0 );
            if( !file.read( reinterpret_cast<char*>( prefix.data() ), prefix.size() ) ) { error = "Truncated terrain recovery header"; return false; }
            uint32_t metadataSize; uint64_t payloadSize;
            if( size < 0 ) { error = "Could not determine terrain recovery file size"; return false; }
            if( !Header( prefix, static_cast<uint64_t>( size ), metadataSize, payloadSize, error ) ) return false;
            prefix.resize( kHeaderSize + metadataSize );
            if( !file.read( reinterpret_cast<char*>( prefix.data() + kHeaderSize ), metadataSize ) ) { error = "Truncated terrain recovery metadata"; return false; }
            if( !Metadata( prefix, out, error ) ) return false;
            error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }

    bool ReadRecovery( const std::filesystem::path& path, RecoveryInfo& outInfo, Heightfield& outField, std::string& error )
    {
        try
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate );
            if( !file ) { error = "Could not open terrain recovery"; return false; }
            const auto size = file.tellg();
            if( size < 0 || static_cast<uint64_t>( size ) > kHeaderSize + kMaxMetadata + kMaxPayload ) { error = "Terrain recovery file exceeds its budget"; return false; }
            std::vector<uint8_t> bytes( static_cast<size_t>( size ) ); file.seekg( 0 );
            if( !bytes.empty() && !file.read( reinterpret_cast<char*>( bytes.data() ), size ) ) { error = "Could not read terrain recovery"; return false; }
            return DeserializeRecovery( bytes, outInfo, outField, error );
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }

    bool WriteRecovery( const std::filesystem::path& path, const RecoveryInfo& info, const Heightfield& field, std::string& error )
    {
        std::vector<uint8_t> bytes;
        if( !SerializeRecovery( info, field, bytes, error ) ) return false;
        std::filesystem::path temporary;
        try
        {
            if( !path.parent_path().empty() ) std::filesystem::create_directories( path.parent_path() );
            temporary = path; temporary += "." + std::to_string( GUID::Generate() ) + ".tmp";
            std::ofstream file( temporary, std::ios::binary | std::ios::trunc );
            if( !file || !file.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() ) ) throw std::runtime_error( "Could not write terrain recovery" );
            file.flush(); if( !file ) throw std::runtime_error( "Could not flush terrain recovery" );
            file.close(); if( !file ) throw std::runtime_error( "Could not close terrain recovery" );
#if USING( ME_PLATFORM_WINDOWS )
            if( !MoveFileExW( temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) ) throw std::runtime_error( "Could not publish terrain recovery" );
#else
            std::filesystem::rename( temporary, path );
#endif
            error.clear(); return true;
        }
        catch( const std::exception& e )
        {
            error = e.what(); std::error_code ignored;
            if( !temporary.empty() ) std::filesystem::remove( temporary, ignored );
            return false;
        }
    }
}
