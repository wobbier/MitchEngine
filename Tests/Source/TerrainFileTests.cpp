#include <doctest/doctest.h>
#include "Terrain/TerrainFileOperations.h"
#include "JSON.h"
#include <filesystem>
#include <fstream>

namespace
{
    struct Files
    {
        std::filesystem::path Root, Source;
        TerrainData::Heightfield Field;
        std::string Text;
        Files()
        {
            static int sequence = 0;
            Root = std::filesystem::path( ".tmp/Tests/TerrainFileOperations" ) / std::to_string( ++sequence );
            std::filesystem::remove_all( Root ); std::filesystem::create_directories( Root / "Destination" );
            Source = Root / "Original.terrain";
            TerrainData::Settings s; s.TileQuads = 4; s.PatchQuads = 2; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
            Field = TerrainData::Heightfield( s ); std::string error;
            for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) REQUIRE( Field.CreateFlatTile( { x, z }, 0, error ) );
            REQUIRE( Field.SetSample( { 0, 0 }, 7.f, error ) );
            REQUIRE( TerrainData::SaveSource( Source, Field, 123, nullptr, Text, error ) );
        }
        ~Files() { std::error_code ignored; std::filesystem::remove_all( Root, ignored ); }
        void Meta( const std::string& text ) { std::ofstream( Source.string() + ".meta" ) << text; }
        void Verify( const std::filesystem::path& path, uint64_t guid )
        {
            TerrainData::Heightfield loaded; uint64_t actual; std::string text, error;
            REQUIRE( TerrainData::LoadSource( path, loaded, actual, text, error ) ); CHECK( actual == guid );
            for( const auto& [coord, tile] : Field.GetTiles() )
            { REQUIRE( loaded.FindTile( coord ) ); CHECK( loaded.FindTile( coord )->Heights == tile.Heights ); }
            json metadata; std::ifstream( path.string() + ".meta" ) >> metadata;
            CHECK( std::stoull( metadata.at( "GUID" ).get<std::string>(), nullptr, 16 ) == guid );
        }
    };
}

TEST_CASE( "Terrain: duplication publishes independent pages and a new sidecar/manifest identity" )
{
    Files files; files.Meta( R"({"GUID":"ffffffffffffffff","Custom":"retain"})" );
    const auto destination = files.Root / "Destination/Independent.terrain";
    TerrainData::SourceTransfer result; std::string error;
    REQUIRE( TerrainData::TransferSource( files.Source, destination, true, result, error ) );
    CHECK( result.AssetGUID != 123 ); CHECK( result.AssetGUID != UINT64_MAX ); CHECK( result.BeforeManifest == files.Text );
    TerrainData::Manifest manifest; REQUIRE( TerrainData::DeserializeManifest( result.AfterManifest, manifest, error ) );
    for( const auto& page : manifest.Pages ) CHECK( page.Source.rfind( "Independent.terrain-data/", 0 ) == 0 );
    std::filesystem::remove_all( files.Root / "Original.terrain-data" );
    files.Verify( destination, result.AssetGUID );
    json metadata; std::ifstream( destination.string() + ".meta" ) >> metadata; CHECK( metadata["Custom"] == "retain" );
    CHECK( std::filesystem::exists( files.Source ) );
}

TEST_CASE( "Terrain: a move preserves the effective metadata GUID and retires source files last" )
{
    Files files; files.Meta( R"({"GUID":"ffffffffffffffff","Custom":123})" );
    const auto destination = files.Root / "Destination/Renamed.terrain";
    TerrainData::SourceTransfer result; std::string error;
    REQUIRE( TerrainData::TransferSource( files.Source, destination, false, result, error ) );
    CHECK( result.AssetGUID == UINT64_MAX ); CHECK_FALSE( std::filesystem::exists( files.Source ) );
    CHECK_FALSE( std::filesystem::exists( files.Source.string() + ".meta" ) );
    CHECK( std::filesystem::exists( files.Root / "Original.terrain-data" ) ); // Other manifests may still reference it.
    files.Verify( destination, UINT64_MAX );
    const auto renamed = files.Root / "Destination/Again.terrain";
    REQUIRE( TerrainData::TransferSource( destination, renamed, false, result, error ) );
    files.Verify( renamed, UINT64_MAX );
}

TEST_CASE( "Terrain: files without sidecars keep their manifest identity when moved" )
{
    Files files; TerrainData::SourceTransfer result; std::string error;
    const auto destination = files.Root / "Moved.terrain";
    REQUIRE( TerrainData::TransferSource( files.Source, destination, false, result, error ) );
    files.Verify( destination, 123 );
}

TEST_CASE( "Terrain: failed file transfers retain source data and reject conflicting destinations" )
{
    Files files; const auto destination = files.Root / "Destination/Blocked.terrain";
    TerrainData::SourceTransfer result; result.AssetGUID = 42; std::string error;
    SUBCASE( "Existing asset" ) { std::ofstream( destination ) << "existing"; }
    SUBCASE( "Existing metadata" ) { std::ofstream( destination.string() + ".meta" ) << "existing"; }
    SUBCASE( "Companion path cannot be created" ) { std::ofstream( files.Root / "Destination/Blocked.terrain-data" ) << "block"; }
    SUBCASE( "Invalid identity" ) { files.Meta( R"({"GUID":"invalid"})" ); }
    SUBCASE( "Corrupt page" )
    {
        TerrainData::Manifest manifest; REQUIRE( TerrainData::DeserializeManifest( files.Text, manifest, error ) );
        std::ofstream( files.Root / manifest.Pages.front().Source, std::ios::trunc ) << "bad page";
    }
    CHECK_FALSE( TerrainData::TransferSource( files.Source, destination, false, result, error ) );
    CHECK( result.AssetGUID == 42 ); CHECK_FALSE( error.empty() ); CHECK( std::filesystem::exists( files.Source ) );
    std::ifstream source( files.Source ); std::string actual( ( std::istreambuf_iterator<char>( source ) ), {} ); CHECK( actual == files.Text );
    for( const auto& entry : std::filesystem::directory_iterator( files.Root / "Destination" ) )
        CHECK( entry.path().filename().string().rfind( ".terrain-transfer-", 0 ) != 0 );
}
