#include <doctest/doctest.h>
#include "Terrain/TerrainRecovery.h"
#include "Terrain/TerrainAsset.h"
#include "Utils/GUID.h"
#include <algorithm>
#include <fstream>

namespace TerrainRecoveryTests
{
    using namespace TerrainData;
    Heightfield Field()
    {
        Settings settings; settings.TileQuads = 4; settings.PatchQuads = 2; settings.TilesX = settings.TilesZ = 2; settings.MinTile = { -1, -1 };
        Heightfield field( settings ); std::string error;
        for( int z = -1; z <= 0; ++z ) for( int x = -1; x <= 0; ++x ) REQUIRE( field.CreateFlatTile( { x, z }, 0, error ) );
        return field;
    }
    RecoveryInfo Info( const Heightfield& base )
    {
        RecoveryInfo info; info.AssetPath = "Assets/Terrain/RecoveryTest.terrain"; info.AssetGUID = 0xfedcba9876543210ull; info.SavedAt = 1792300000;
        Manifest manifest; manifest.AssetGUID = info.AssetGUID; manifest.Config = base.GetSettings();
        for( const auto& [coord, tile] : base.GetTiles() ) manifest.Pages.push_back( { coord, std::to_string( coord.X ) + "_" + std::to_string( coord.Z ) + ".thp" } );
        std::string error; REQUIRE( SerializeManifest( manifest, info.BaseManifest, error ) ); return info;
    }
    void Equal( const Heightfield& a, const Heightfield& b )
    {
        REQUIRE( a.GetTiles().size() == b.GetTiles().size() );
        for( const auto& [coord, tile] : a.GetTiles() ) { REQUIRE( b.FindTile( coord ) ); CHECK( tile.Heights == b.FindTile( coord )->Heights ); }
    }
    struct Files
    {
        std::filesystem::path Root = std::filesystem::path( ".tmp/Tests/TerrainRecovery" ) / std::to_string( EngineGUID::Generate() );
        Files() { std::filesystem::create_directories( Root ); }
        ~Files() { std::error_code error; std::filesystem::remove_all( Root, error ); }
    };
    std::vector<uint8_t> Read( const std::filesystem::path& path )
    {
        std::ifstream file( path, std::ios::binary ); return { std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
    }
    void Put( std::vector<uint8_t>& bytes, size_t offset, uint64_t value, int count )
    {
        for( int i = 0; i < count; ++i ) bytes[offset + i] = static_cast<uint8_t>( value >> ( 8 * i ) );
    }
    uint64_t Hash( std::span<const uint8_t> bytes )
    {
        uint64_t hash = 14695981039346656037ull;
        for( auto byte : bytes ) { hash ^= byte; hash *= 1099511628211ull; } return hash;
    }
}
using namespace TerrainRecoveryTests;

TEST_CASE( "Terrain: recovery preserves exact four-tile heights and the original save conflict precondition" )
{
    auto field = Field(); const auto info = Info( field ); std::string error;
    REQUIRE( field.SetSample( { 0, 0 }, 3.14159f, error ) ); REQUIRE( field.SetSample( { -2, 1 }, -7.125f, error ) );
    std::vector<uint8_t> bytes; REQUIRE( SerializeRecovery( info, field, bytes, error ) );
    RecoveryInfo decoded; Heightfield restored;
    REQUIRE( DeserializeRecovery( bytes, decoded, restored, error ) ); Equal( field, restored );
    CHECK( decoded.AssetGUID == info.AssetGUID ); CHECK( decoded.AssetPath == info.AssetPath ); CHECK( decoded.SavedAt == info.SavedAt ); CHECK( decoded.BaseManifest == info.BaseManifest );
    // A copied source may carry a different embedded GUID from its .meta identity.
    auto duplicate = info; duplicate.AssetGUID = 42;
    REQUIRE( SerializeRecovery( duplicate, field, bytes, error ) ); REQUIRE( DeserializeRecovery( bytes, decoded, restored, error ) ); CHECK( decoded.AssetGUID == 42 ); Equal( field, restored );
}

TEST_CASE( "Terrain: corrupt truncated and oversized recovery records preserve caller outputs" )
{
    const auto field = Field(); const auto info = Info( field ); std::string error; std::vector<uint8_t> valid;
    REQUIRE( SerializeRecovery( info, field, valid, error ) );
    for( int corruption = 0; corruption < 7; ++corruption )
    {
        auto bytes = valid;
        if( corruption == 0 ) bytes[36] ^= 1;
        if( corruption == 1 ) bytes.back() ^= 1;
        if( corruption == 2 ) bytes.pop_back();
        if( corruption == 3 ) bytes.push_back( 0 );
        if( corruption == 4 ) Put( bytes, 8, 0xffffffff, 4 );
        if( corruption == 5 ) Put( bytes, 12, 0xffffffffffffffffull, 8 );
        if( corruption == 6 ) Put( bytes, 4, 2, 4 );
        RecoveryInfo output = info; output.AssetPath = "sentinel"; Heightfield restored = field;
        CHECK_FALSE( DeserializeRecovery( bytes, output, restored, error ) ); CHECK( output.AssetPath == "sentinel" ); Equal( field, restored ); CHECK_FALSE( error.empty() );
    }
}

TEST_CASE( "Terrain: recovery rejects a validly checksummed identity that disagrees with the height payload" )
{
    const auto field = Field(); const auto info = Info( field ); std::string error; std::vector<uint8_t> bytes;
    REQUIRE( SerializeRecovery( info, field, bytes, error ) );
    const std::string marker = "fedcba9876543210";
    auto it = std::search( bytes.begin() + 36, bytes.end(), marker.begin(), marker.end() ); REQUIRE( it != bytes.end() );
    *it = 'e'; // Same metadata length and still a valid nonzero GUID.
    const size_t size = size_t( bytes[8] ) | size_t( bytes[9] ) << 8 | size_t( bytes[10] ) << 16 | size_t( bytes[11] ) << 24;
    Put( bytes, 20, Hash( std::span<const uint8_t>( bytes ).subspan( 36, size ) ), 8 );
    RecoveryInfo output; Heightfield restored;
    CHECK_FALSE( DeserializeRecovery( bytes, output, restored, error ) ); CHECK( error.find( "identity" ) != std::string::npos );
}

TEST_CASE( "Terrain: invalid recovery inputs and missing source pages never replace serialized output" )
{
    auto field = Field(); const auto info = Info( field ); std::string error; std::vector<uint8_t> bytes{ 1, 2, 3 };
    auto invalid = info; invalid.AssetGUID = 0;
    CHECK_FALSE( SerializeRecovery( invalid, field, bytes, error ) ); CHECK( bytes == std::vector<uint8_t>{ 1, 2, 3 } );
    invalid = info; invalid.AssetPath = "Assets/Terrain/ReadOnly.terrainc";
    CHECK_FALSE( SerializeRecovery( invalid, field, bytes, error ) );
    invalid = info; invalid.BaseManifest = "broken";
    CHECK_FALSE( SerializeRecovery( invalid, field, bytes, error ) );
    Settings other = field.GetSettings(); other.SampleSpacing = 2;
    Heightfield different( other );
    CHECK_FALSE( SerializeRecovery( info, different, bytes, error ) );
    field.UnloadTile( { 0, 0 } );
    CHECK_FALSE( SerializeRecovery( info, field, bytes, error ) ); CHECK( bytes == std::vector<uint8_t>{ 1, 2, 3 } );
}

TEST_CASE( "Terrain: atomic recovery file replacement retains the last valid snapshot on failure" )
{
    Files files; const auto path = files.Root / "Snapshot.terrain-recovery";
    auto field = Field(); auto info = Info( field ); std::string error;
    REQUIRE( WriteRecovery( path, info, field, error ) ); const auto original = Read( path );
    auto invalid = info; invalid.AssetGUID = 0;
    CHECK_FALSE( WriteRecovery( path, invalid, field, error ) ); CHECK( Read( path ) == original );
    REQUIRE( field.SetSample( { 0, 0 }, 8, error ) ); ++info.SavedAt;
    REQUIRE( WriteRecovery( path, info, field, error ) );
    RecoveryInfo restoredInfo; Heightfield restored;
    REQUIRE( ReadRecovery( path, restoredInfo, restored, error ) ); CHECK( restoredInfo.SavedAt == info.SavedAt ); Equal( restored, field );
    for( const auto& file : std::filesystem::directory_iterator( files.Root ) ) CHECK( file.path().extension() == ".terrain-recovery" );
}

TEST_CASE( "Terrain: recovery metadata can be listed while a corrupted payload remains unconsumed" )
{
    Files files; const auto path = files.Root / "Snapshot.terrain-recovery";
    const auto field = Field(); const auto info = Info( field ); std::string error;
    REQUIRE( WriteRecovery( path, info, field, error ) ); auto bytes = Read( path ); bytes.back() ^= 1;
    std::ofstream file( path, std::ios::binary ); file.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() ); file.close();
    RecoveryInfo listed; REQUIRE( InspectRecovery( path, listed, error ) ); CHECK( listed.AssetPath == info.AssetPath );
    Heightfield restored = field; listed.AssetPath = "sentinel";
    CHECK_FALSE( ReadRecovery( path, listed, restored, error ) ); CHECK( listed.AssetPath == "sentinel" ); Equal( field, restored ); CHECK( std::filesystem::exists( path ) );
}

TEST_CASE( "Terrain: recovered source refuses to overwrite external edits and can be saved as a new asset" )
{
    Files files; const auto source = files.Root / "Source.terrain", snapshot = files.Root / "Snapshot.terrain-recovery";
    auto field = Field(); auto info = Info( field ); std::string error, text;
    REQUIRE( SaveSource( source, field, info.AssetGUID, nullptr, text, error ) ); info.BaseManifest = text;
    REQUIRE( field.SetSample( { 0, 0 }, 1, error ) ); REQUIRE( WriteRecovery( snapshot, info, field, error ) );
    auto external = field; REQUIRE( external.SetSample( { 0, 0 }, 7, error ) ); std::string changed;
    REQUIRE( SaveSource( source, external, info.AssetGUID, &text, changed, error ) );
    RecoveryInfo recovered; Heightfield restored; REQUIRE( ReadRecovery( snapshot, recovered, restored, error ) );
    std::string output = "unchanged";
    CHECK_FALSE( SaveSource( source, restored, recovered.AssetGUID, &recovered.BaseManifest, output, error ) ); CHECK( output == "unchanged" );
    Heightfield current; uint64_t guid; std::string currentText;
    REQUIRE( LoadSource( source, current, guid, currentText, error ) ); Equal( current, external );
    REQUIRE( SaveSource( files.Root / "RecoveredCopy.terrain", restored, 42, nullptr, output, error ) );
    REQUIRE( LoadSource( files.Root / "RecoveredCopy.terrain", current, guid, currentText, error ) ); Equal( current, field ); CHECK( guid == 42 );
}
