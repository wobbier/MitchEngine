#include "PCH.h"
#include "TerrainAsset.h"
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
        std::vector<uint8_t> Read( const std::filesystem::path& path )
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate );
            if( !file ) throw std::runtime_error( "Could not read " + path.string() );
            const auto size = file.tellg();
            if( size < 0 || static_cast<uint64_t>( size ) > 1024ull * 1024 * 1024 ) throw std::runtime_error( "Terrain source file is too large" );
            std::vector<uint8_t> bytes( static_cast<size_t>( size ) );
            file.seekg( 0 );
            if( !bytes.empty() && !file.read( reinterpret_cast<char*>( bytes.data() ), size ) ) throw std::runtime_error( "Incomplete terrain source read" );
            return bytes;
        }
        std::string ReadText( const std::filesystem::path& path )
        {
            const auto bytes = Read( path );
            return std::string( bytes.begin(), bytes.end() );
        }
        void Write( const std::filesystem::path& path, const void* bytes, size_t size )
        {
            std::ofstream file( path, std::ios::binary | std::ios::trunc );
            if( !file || !file.write( static_cast<const char*>( bytes ), static_cast<std::streamsize>( size ) ) )
                throw std::runtime_error( "Could not write " + path.string() );
            file.flush();
            if( !file ) throw std::runtime_error( "Could not flush " + path.string() );
            file.close();
            if( !file ) throw std::runtime_error( "Could not close " + path.string() );
        }
    }

    bool LoadSource( const std::filesystem::path& path, Heightfield& outField, uint64_t& outGUID, std::string& outText, std::string& error )
    {
        try
        {
            const std::string text = ReadText( path );
            Manifest manifest;
            if( !DeserializeManifest( text, manifest, error ) ) return false;
            Heightfield field( manifest.Config );
            for( const auto& reference : manifest.Pages )
            {
                TilePage page;
                if( !DeserializePage( Read( path.parent_path() / reference.Source ), manifest.Config, page, error ) ) return false;
                if( page.Coordinate != reference.Coordinate ) { error = "Source page coordinates disagree with the manifest"; return false; }
                if( !field.LoadTile( page.Coordinate, std::move( page.Heights ), error ) ) return false;
            }
            field.ClearDirty();
            outField = std::move( field ); outGUID = manifest.AssetGUID; outText = text;
            error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }

    bool SaveSource( const std::filesystem::path& path, const Heightfield& field, uint64_t guid,
        const std::string* expected, std::string& outText, std::string& error )
    {
        namespace fs = std::filesystem;
        fs::path generation, temporary;
        bool published = false, ownsGeneration = false;
        try
        {
            if( path.extension() != ".terrain" ) throw std::runtime_error( "Save authoring data to a .terrain asset" );
            auto checkConflict = [&]() {
                if( expected )
                {
                    if( !fs::exists( path ) || ReadText( path ) != *expected )
                        throw std::runtime_error( "Terrain changed on disk. Save a copy or revert before saving" );
                }
                else if( fs::exists( path ) ) throw std::runtime_error( "A terrain asset already exists at that path" );
            };
            checkConflict();
            const auto& s = field.GetSettings();
            if( field.GetTiles().size() != static_cast<size_t>( s.TilesX ) * s.TilesZ ) throw std::runtime_error( "Every source tile must be resident before saving" );
            if( !path.parent_path().empty() ) fs::create_directories( path.parent_path() );
            std::ostringstream id; id << std::hex << GUID::Generate();
            const fs::path relative = fs::path( path.stem().string() + ".terrain-data" ) / id.str();
            generation = path.parent_path() / relative;
            if( !fs::create_directories( generation ) ) throw std::runtime_error( "Terrain generation already exists" );
            ownsGeneration = true;
            Manifest manifest; manifest.AssetGUID = guid; manifest.Config = s;
            for( const auto& [coordinate, tile] : field.GetTiles() )
            {
                const auto name = std::to_string( coordinate.X ) + "_" + std::to_string( coordinate.Z ) + ".thp";
                std::vector<uint8_t> bytes;
                if( !SerializePage( field, coordinate, bytes, error ) ) throw std::runtime_error( error );
                Write( generation / name, bytes.data(), bytes.size() );
                manifest.Pages.push_back( { coordinate, ( relative / name ).generic_string() } );
            }
            std::string text;
            if( !SerializeManifest( manifest, text, error ) ) throw std::runtime_error( error );
            temporary = path; temporary += "." + id.str() + ".tmp";
            Write( temporary, text.data(), text.size() );
            checkConflict();
#if USING( ME_PLATFORM_WINDOWS )
            // std::filesystem::rename cannot replace an existing file on Windows.
            if( !MoveFileExW( temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
                throw std::runtime_error( "Could not publish terrain manifest (Windows error " + std::to_string( GetLastError() ) + ")" );
#else
            fs::rename( temporary, path );
#endif
            published = true;
            outText = std::move( text ); error.clear(); return true;
        }
        catch( const std::exception& e )
        {
            error = e.what();
            std::error_code ignored;
            if( !temporary.empty() ) fs::remove( temporary, ignored );
            if( !published && ownsGeneration ) fs::remove_all( generation, ignored );
            return false;
        }
    }
}
