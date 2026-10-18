#include "PCH.h"
#include "TerrainFileOperations.h"
#include "JSON.h"
#include "Utils/GUID.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace TerrainData
{
    namespace
    {
        std::string ReadText( const std::filesystem::path& path )
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate );
            if( !file ) throw std::runtime_error( "Could not read terrain asset metadata" );
            const auto size = file.tellg();
            if( size < 0 || size > 8 * 1024 * 1024 ) throw std::runtime_error( "Terrain asset text exceeds its 8 MiB transfer budget" );
            std::string text( static_cast<size_t>( size ), '\0' ); file.seekg( 0 );
            if( !text.empty() && !file.read( text.data(), size ) ) throw std::runtime_error( "Incomplete terrain asset text read" );
            return text;
        }

        void WriteText( const std::filesystem::path& path, const std::string& text )
        {
            std::ofstream file( path, std::ios::binary | std::ios::trunc );
            if( !file || !file.write( text.data(), text.size() ) ) throw std::runtime_error( "Could not stage terrain metadata" );
            file.flush(); if( !file ) throw std::runtime_error( "Could not flush terrain metadata" );
            file.close(); if( !file ) throw std::runtime_error( "Could not close terrain metadata" );
        }

        bool Present( const std::filesystem::path& path )
        {
            std::error_code ec;
            const auto status = std::filesystem::symlink_status( path, ec );
            if( ec && ec != std::errc::no_such_file_or_directory ) throw std::filesystem::filesystem_error( "Could not inspect terrain path", path, ec );
            return status.type() != std::filesystem::file_type::not_found;
        }

        uint64_t Identity( const json& metadata )
        {
            const auto text = metadata.at( "GUID" ).get<std::string>();
            if( text.size() != 16 || text.find_first_not_of( "0123456789abcdefABCDEF" ) != std::string::npos )
                throw std::runtime_error( "Invalid terrain metadata GUID" );
            const uint64_t guid = std::stoull( text, nullptr, 16 );
            if( !guid ) throw std::runtime_error( "Terrain metadata GUID must be nonzero" );
            return guid;
        }
    }


    bool TransferSource( const std::filesystem::path& source, const std::filesystem::path& destination, bool duplicate,
        SourceTransfer& out, std::string& error )
    {
        namespace fs = std::filesystem;
        fs::path staging, generation, retiredSource, retiredMeta;
        fs::path from, to, fromMeta, toMeta;
        bool ownsCompanionRoot = false, ownsStaging = false, published = false, metadataPublished = false, ownsGeneration = false, sourceRetired = false, metaRetired = false;
        try
        {
            from = fs::absolute( source ).lexically_normal(); to = fs::absolute( destination ).lexically_normal();
            fromMeta = from.string() + ".meta"; toMeta = to.string() + ".meta";
            if( from.extension() != ".terrain" || to.extension() != ".terrain" || from == to )
                throw std::runtime_error( "Move or duplicate a .terrain source to a different .terrain path" );
            auto checkDestination = [&]() {
                if( Present( to ) || Present( toMeta ) ) throw std::runtime_error( "Terrain destination or metadata already exists" );
            };
            checkDestination();
            const bool hadMeta = fs::exists( fromMeta );
            const std::string originalMeta = hadMeta ? ReadText( fromMeta ) : std::string();
            json metadata = hadMeta ? json::parse( originalMeta ) : json::object();
            if( !metadata.is_object() ) throw std::runtime_error( "Invalid terrain metadata object" );
            ReadText( from ); // Bound manifest input before the full source load.
            Heightfield field; uint64_t sourceGUID; SourceTransfer result;
            if( !LoadSource( from, field, sourceGUID, result.BeforeManifest, error ) ) return false;
            const uint64_t guid = hadMeta ? Identity( metadata ) : sourceGUID;
            result.AssetGUID = duplicate ? GUID::Generate() : guid;
            std::ostringstream id; id << std::hex << std::setw( 16 ) << std::setfill( '0' ) << result.AssetGUID;
            metadata["GUID"] = id.str(); metadata["FileType"] = "terrain";
            const std::string nextMeta = !duplicate && hadMeta ? originalMeta : metadata.dump( 4 );
            auto checkSource = [&]() {
                if( ReadText( from ) != result.BeforeManifest || fs::exists( fromMeta ) != hadMeta || ( hadMeta && ReadText( fromMeta ) != originalMeta ) )
                    throw std::runtime_error( "Terrain source or metadata changed during the file operation" );
            };
            if( !fs::is_directory( to.parent_path() ) ) throw std::runtime_error( "Terrain destination folder does not exist" );
            const std::string token = std::to_string( GUID::Generate() );
            staging = to.parent_path() / ( ".terrain-transfer-" + token );
            if( !fs::create_directory( staging ) ) throw std::runtime_error( "Terrain transfer staging directory already exists" );
            ownsStaging = true;
            const fs::path staged = staging / to.filename();
            if( !SaveSource( staged, field, result.AssetGUID, nullptr, result.AfterManifest, error ) ) throw std::runtime_error( error );
            WriteText( staging / "Asset.meta", nextMeta );
            Manifest manifest;
            if( !DeserializeManifest( result.AfterManifest, manifest, error ) ) throw std::runtime_error( error );
            const fs::path relativeGeneration = fs::path( manifest.Pages.front().Source ).parent_path();
            generation = to.parent_path() / relativeGeneration;
            ownsCompanionRoot = fs::create_directories( generation.parent_path() );
            if( fs::exists( generation ) ) throw std::runtime_error( "Terrain destination generation already exists" );
            checkSource(); checkDestination();
            fs::rename( staging / relativeGeneration, generation ); ownsGeneration = true;
            fs::rename( staging / "Asset.meta", toMeta ); metadataPublished = true;
            // The manifest is the publication point. All referenced data and metadata exist first.
            if( Present( to ) ) throw std::runtime_error( "Terrain destination appeared during publication" );
            fs::rename( staged, to ); published = true;
            if( !duplicate )
            {
                checkSource();
                retiredSource = from.parent_path() / ( "." + from.filename().string() + "." + token + ".moved" );
                retiredMeta = retiredSource.string() + ".meta";
                if( fs::exists( retiredSource ) || fs::exists( retiredMeta ) ) throw std::runtime_error( "Terrain retirement path already exists" );
                fs::rename( from, retiredSource ); sourceRetired = true;
                if( hadMeta ) { fs::rename( fromMeta, retiredMeta ); metaRetired = true; }
            }
            std::error_code ignored;
            if( sourceRetired ) fs::remove( retiredSource, ignored );
            if( metaRetired ) fs::remove( retiredMeta, ignored );
            fs::remove_all( staging, ignored );
            out = std::move( result ); error.clear(); return true;
        }
        catch( const std::exception& e )
        {
            error = e.what(); std::error_code ec;
            bool restored = true;
            if( metaRetired ) { fs::rename( retiredMeta, fromMeta, ec ); restored &= !ec; }
            if( sourceRetired ) { ec.clear(); fs::rename( retiredSource, from, ec ); restored &= !ec; }
            if( restored )
            {
                if( published )
                {
                    ec.clear(); fs::remove( to, ec );
                    if( ec ) { restored = false; error += ". The complete destination was retained at " + to.string(); }
                }
                if( restored )
                {
                    if( metadataPublished ) fs::remove( toMeta, ec );
                    if( ownsGeneration ) fs::remove_all( generation, ec );
                    if( ownsCompanionRoot ) fs::remove( generation.parent_path(), ec );
                }
            }
            else error += ". Could not restore the original location; the complete destination was retained at " + to.string();
            if( ownsStaging ) fs::remove_all( staging, ec );
            return false;
        }
    }
}
