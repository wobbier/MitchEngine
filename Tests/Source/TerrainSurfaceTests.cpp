#include <doctest/doctest.h>
#include "Terrain/TerrainSurface.h"
#include "Terrain/TerrainEditing.h"
#include "Terrain/TerrainAsset.h"
#include "Terrain/TerrainFileOperations.h"
#include "Terrain/TerrainRecovery.h"
#include "Terrain/TerrainStreamArchive.h"
#include "Terrain/ResidentTerrain.h"
#include "Resources/TerrainResource.h"
#include "Utils/GUID.h"
#include <fstream>
#include <limits>
#include <numeric>

namespace TerrainSurfaceTests
{
    using namespace TerrainData;
    Heightfield Flat()
    {
        Settings s; s.TileQuads = 8; s.PatchQuads = 4; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
        Heightfield field( s ); std::string error;
        for( int z = -1; z <= 0; ++z ) for( int x = -1; x <= 0; ++x ) REQUIRE( field.CreateFlatTile( { x, z }, 0, error ) );
        return field;
    }
    struct Files
    {
        std::filesystem::path Root = std::filesystem::path( ".tmp/Tests/TerrainSurface" ) / std::to_string( GUID::Generate() );
        Files() { std::filesystem::create_directories( Root ); }
        ~Files() { std::error_code ec; std::filesystem::remove_all( Root, ec ); }
    };
    std::string Text( const std::filesystem::path& path )
    {
        std::ifstream file( path, std::ios::binary ); return { std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
    }
}
using namespace TerrainSurfaceTests;

TEST_CASE( "Terrain surface: blending normalizes all four channels with a defined zero-sum fallback" )
{
    CHECK( NormalizeWeights( {} ) == LayerWeights{ 255, 0, 0, 0 } );
    CHECK( NormalizeWeights( { -1, std::numeric_limits<float>::quiet_NaN(), 0, 0 } ) == LayerWeights{ 255, 0, 0, 0 } );
    for( unsigned layer = 0; layer < 4; ++layer )
    {
        auto weights = NormalizeWeights( { 0.123f, 0.456f, 0.789f, 0.234f } );
        for( int i = 0; i <= 100; ++i )
        {
            weights = BlendLayer( weights, layer, i / 100.f, false );
            CHECK( std::accumulate( weights.begin(), weights.end(), 0 ) == 255 );
        }
        CHECK( weights[layer] == 255 );
        weights = BlendLayer( weights, layer, 1, true );
        CHECK( weights[layer] == 0 ); CHECK( std::accumulate( weights.begin(), weights.end(), 0 ) == 255 );
    }
}

TEST_CASE( "Terrain surface: distance-spaced painting is input-rate independent and cancellation restores the source" )
{
    auto a = Flat(), b = a; std::string error; BrushSettings brush; brush.Tool = BrushTool::Paint;
    brush.Layer = 2; brush.Radius = 3; brush.Strength = 0.7f; brush.Spacing = 0.25f;
    BrushStroke first, second;
    REQUIRE( first.Begin( a, brush, -4, 0, error ) ); REQUIRE( first.Move( a, 4, 0, error ) ); first.Finish();
    REQUIRE( second.Begin( b, brush, -4, 0, error ) );
    for( int i = -15; i <= 16; ++i ) REQUIRE( second.Move( b, i * 0.25, 0, error ) );
    second.Finish(); REQUIRE( a.GetSurface() ); REQUIRE( b.GetSurface() ); CHECK( *a.GetSurface() == *b.GetSurface() );
    CHECK( a.GetSurface()->Get( { 0, 0 } )[2] > 0 );
    const auto before = a.GetSurface(); brush.Tool = BrushTool::Erase;
    REQUIRE( first.Begin( a, brush, 0, 0, error ) ); CHECK( a.GetSurface()->Get( { 0, 0 } )[2] < before->Get( { 0, 0 } )[2] );
    first.Cancel( a ); CHECK( a.GetSurface() == before );
    auto empty = Flat(); brush.Tool = BrushTool::Paint;
    REQUIRE( first.Begin( empty, brush, 0, 0, error ) ); first.Cancel( empty ); CHECK_FALSE( empty.GetSurface() );
    brush.Layer = 4; CHECK_FALSE( first.Begin( empty, brush, 0, 0, error ) );
    for( const auto& [coord, tile] : a.GetTiles() ) CHECK( tile.Heights == b.FindTile( coord )->Heights );
}

TEST_CASE( "Terrain surface: patch borders match and painting retains geometry and collision snapshots" )
{
    auto field = Flat(); std::string error; std::shared_ptr<const ResidentTerrain> before, after;
    REQUIRE( BuildResidentTerrain( field, 123, before, error ) );
    auto surface = std::make_shared<Surface>();
    for( int z = -8; z <= 8; ++z ) surface->Set( { 0, z }, { 10, 20, 30, 195 } );
    REQUIRE( field.SetSurface( surface, error ) ); REQUIRE( UpdateResidentTerrain( field, 123, before, after, error ) );
    CHECK( after->BuiltPatches == 0 ); CHECK( after->BuiltCollisionTiles == 0 );
    CHECK( after->Patches == before->Patches ); CHECK( after->CollisionTiles == before->CollisionTiles );
    CHECK_FALSE( before->Heights->GetSurface() ); REQUIRE( after->Heights->GetSurface() );
    const auto left = BuildSurfacePatch( *surface, { -4, -4 }, 4 ), right = BuildSurfacePatch( *surface, { 0, -4 }, 4 );
    REQUIRE( left.size() == 25 ); REQUIRE( right.size() == 25 );
    for( int z = 0; z <= 4; ++z ) CHECK( left[z * 5 + 4] == right[z * 5] );
}

TEST_CASE( "Terrain surface: codec rejects corruption invalid weights and unsafe source references atomically" )
{
    auto field = Flat(); Surface surface; surface.Set( { -1, 0 }, { 0, 255, 0, 0 } );
    std::vector<uint8_t> bytes; std::string error; REQUIRE( SerializeSurface( surface, field.GetSettings(), bytes, error ) );
    Surface decoded; REQUIRE( DeserializeSurface( bytes, field.GetSettings(), decoded, error ) ); CHECK( decoded == surface );
    const auto good = bytes; bytes.back() ^= 1;
    CHECK_FALSE( DeserializeSurface( bytes, field.GetSettings(), decoded, error ) ); CHECK( decoded == surface );
    bytes = good; bytes.pop_back(); CHECK_FALSE( DeserializeSurface( bytes, field.GetSettings(), decoded, error ) );
    surface.Samples[{ 0, 0 }] = { 1, 2, 3, 4 }; const auto output = bytes;
    CHECK_FALSE( SerializeSurface( surface, field.GetSettings(), bytes, error ) ); CHECK( bytes == output );
    Manifest manifest; manifest.AssetGUID = 1; manifest.Config = field.GetSettings();
    for( const auto& [coord, tile] : field.GetTiles() ) manifest.Pages.push_back( { coord, std::to_string( coord.X ) + "_" + std::to_string( coord.Z ) + ".thp" } );
    manifest.SurfaceSource = "../Surface.tsurface"; std::string text = "sentinel";
    CHECK_FALSE( SerializeManifest( manifest, text, error ) ); CHECK( text == "sentinel" );
}

TEST_CASE( "Terrain surface: atomic source generations copy moves recovery and cook guards preserve paint" )
{
    Files files; auto field = Flat(); auto surface = std::make_shared<Surface>(); std::string error;
    surface->Layers[1].Name = "Wet earth"; surface->Layers[1].Roughness = 0.2f;
    surface->BaseWeights = { 0, 255, 0, 0 }; surface->Set( { 0, 0 }, { 0, 0, 0, 255 } );
    REQUIRE( field.SetSurface( surface, error ) );
    const auto source = files.Root / "Source.terrain"; std::string manifest;
    REQUIRE( SaveSource( source, field, 123, nullptr, manifest, error ) );
    Heightfield restored; uint64_t guid = 0; std::string text;
    REQUIRE( LoadSource( source, restored, guid, text, error ) ); REQUIRE( restored.GetSurface() ); CHECK( *restored.GetSurface() == *surface );
    SourceTransfer transfer; REQUIRE( TransferSource( source, files.Root / "Copy.terrain", true, transfer, error ) );
    REQUIRE( TransferSource( files.Root / "Copy.terrain", files.Root / "Moved.terrain", false, transfer, error ) );
    REQUIRE( LoadSource( files.Root / "Moved.terrain", restored, guid, text, error ) ); CHECK( *restored.GetSurface() == *surface );
    RecoveryInfo info; info.AssetPath = "Assets/Terrain/Surface.terrain"; info.AssetGUID = 123; info.BaseManifest = manifest;
    std::vector<uint8_t> bytes; REQUIRE( SerializeRecovery( info, field, bytes, error ) ); RecoveryInfo decoded;
    REQUIRE( DeserializeRecovery( bytes, decoded, restored, error ) ); REQUIRE( restored.GetSurface() ); CHECK( *restored.GetSurface() == *surface );
    CHECK( decoded.BaseManifest == manifest );
    std::vector<uint8_t> cooked{ 1, 2, 3 }; CHECK_FALSE( CookHeightfield( field, 123, HeightEncoding::Float32, cooked, error ) ); CHECK( cooked == std::vector<uint8_t>{ 1, 2, 3 } );
    const auto archive = files.Root / "Surface.terrainc"; { std::ofstream out( archive ); out << "sentinel"; }
    CHECK_FALSE( CookStreamArchive( archive, field, 123, 1, error ) ); CHECK( Text( archive ) == "sentinel" );
    CHECK_FALSE( CookPagedStreamArchive( archive, field, 123, 1, error ) ); CHECK( Text( archive ) == "sentinel" );
    // External manifest edits still prevent publication, including its surface reference.
    { std::ofstream out( source ); out << "external"; }
    CHECK_FALSE( SaveSource( source, field, 123, &manifest, text, error ) ); CHECK( Text( source ) == "external" );
}


TEST_CASE( "Terrain surface: runtime resource loads paint without an editor preview and rejects a broken companion" )
{
    Files files; auto field = Flat(); auto surface = std::make_shared<Surface>(); std::string error, text;
    surface->Set( { 0, 0 }, { 0, 0, 255, 0 } ); surface->Layers[2].Name = "Runtime rock";
    REQUIRE( field.SetSurface( surface, error ) ); const auto path = files.Root / "Runtime.terrain";
    REQUIRE( SaveSource( path, field, 999, nullptr, text, error ) );
    TerrainResource resource( Path( path.generic_string() ) );
    REQUIRE( resource.LoadAsync() ); REQUIRE( resource.FinishAsyncLoad() );
    REQUIRE( resource.GetData() ); REQUIRE( resource.GetData()->Heights->GetSurface() );
    CHECK( *resource.GetData()->Heights->GetSurface() == *surface );
    const auto before = resource.GetData(); Manifest manifest;
    REQUIRE( DeserializeManifest( text, manifest, error ) );
    { std::ofstream out( path.parent_path() / manifest.SurfaceSource, std::ios::binary ); out << "broken"; }
    CHECK_FALSE( resource.LoadAsync() ); CHECK( resource.GetData() == before );
}
