#include <doctest/doctest.h>
#include "Terrain/TerrainStreamCache.h"
#include "Terrain/TerrainStreamScheduler.h"
#include "Terrain/TerrainHierarchy.h"
#include "Terrain/TerrainLod.h"
#include "Terrain/TerrainSurface.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <tuple>
#include <set>

namespace
{
    struct HierarchyFixture
    {
        TerrainData::Heightfield Field;
        std::filesystem::path Directory, File;
        std::shared_ptr<const TerrainData::StreamArchive> Archive;
        HierarchyFixture( bool painted = false, bool rectangle = false ) : Field( Settings( rectangle ) )
        {
            static uint64_t sequence = 0; Directory = std::filesystem::path( ".tmp/Tests/TerrainHierarchy" ) / std::to_string( ++sequence );
            std::filesystem::remove_all( Directory ); std::filesystem::create_directories( Directory ); File = Directory / "Runtime.terrainc";
            const auto& s = Field.GetSettings(); std::string error;
            for( uint32_t x = 0; x < s.TilesX; ++x ) for( uint32_t z = 0; z < s.TilesZ; ++z ) REQUIRE( Field.CreateFlatTile( { s.MinTile.X + int32_t( x ), s.MinTile.Z + int32_t( z ) }, 0, error ) );
            for( auto z = s.MinSample().Z; z <= s.MaxSample().Z; ++z ) for( auto x = s.MinSample().X; x <= s.MaxSample().X; ++x )
                REQUIRE( Field.SetSample( { x, z }, float( std::sin( x * 0.13 ) * std::cos( z * 0.11 ) ), error ) );
            if( painted )
            {
                auto surface = std::make_shared<TerrainData::Surface>(); surface->BaseWeights = { 90, 80, 60, 25 };
                for( auto z = s.MinSample().Z; z <= s.MaxSample().Z; ++z ) for( auto x = s.MinSample().X; x <= s.MaxSample().X; ++x ) if( x % 3 == 0 ) surface->Set( { x, z }, { 0, 0, 255, 0 } );
                REQUIRE( Field.SetSurface( surface, error ) );
            }
            REQUIRE_MESSAGE( TerrainData::CookHierarchicalStreamArchive( File, Field, 123, 1, error ), error );
            REQUIRE_MESSAGE( TerrainData::OpenStreamArchive( File, Archive, error ), error );
        }
        static TerrainData::Settings Settings( bool rectangle )
        {
            TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 8; s.MinTile = { -3, -2 }; s.TilesX = rectangle ? 7 : 8; s.TilesZ = rectangle ? 5 : 8; return s;
        }
    };


    void Partition( const TerrainData::StreamArchive& archive, const std::vector<uint32_t>& cut )
    {
        const auto& s = archive.Config;
        for( uint32_t x = 0; x < s.TilesX; ++x ) for( uint32_t z = 0; z < s.TilesZ; ++z )
        {
            unsigned covered = 0;
            for( auto i : cut )
            {
                const auto& n = archive.Nodes[i]; const int64_t side = int64_t( 1 ) << n.Level, px = int64_t( s.MinTile.X ) + x, pz = int64_t( s.MinTile.Z ) + z;
                covered += px >= n.Coordinate.X && px < n.Coordinate.X + side && pz >= n.Coordinate.Z && pz < n.Coordinate.Z + side;
            }
            CHECK( covered == 1 );
        }
    }


    std::vector<uint32_t> Requests( const TerrainData::StreamArchive& archive, const TerrainData::CoverageView& view )
    {
        std::string error; std::vector<uint32_t> requests; REQUIRE( TerrainData::SelectCoverageRequests( archive, { &view, 1 }, 64, requests, error ) ); return requests;
    }


    std::set<std::pair<int64_t, uint32_t>> EdgeVertices( const TerrainData::ResidentPatch& patch, uint32_t patchQuads, const TerrainData::PatchLodChoice& choice, uint8_t edge, int64_t begin, int64_t end )
    {
        const uint32_t q = uint32_t( ( uint64_t( patchQuads ) << patch.SpanLevel ) >> patch.BaseLod );
        std::vector<uint32_t> indices; std::string error;
        REQUIRE_MESSAGE( TerrainData::BuildStitchedIndices( q, 1u << ( choice.Level - patch.BaseLod ), choice.Edges, indices, error ), error );
        std::set<std::pair<int64_t, uint32_t>> result;
        for( auto i : indices )
        {
            const uint32_t x = i % ( q + 1 ), z = i / ( q + 1 );
            if( ( edge == 0 && x != 0 ) || ( edge == 1 && x != q ) || ( edge == 2 && z != 0 ) || ( edge == 3 && z != q ) ) continue;
            const int64_t along = edge < 2 ? patch.Mesh.SampleOrigin.Z + ( int64_t( z ) << patch.BaseLod ) : patch.Mesh.SampleOrigin.X + ( int64_t( x ) << patch.BaseLod );
            if( along >= begin && along <= end ) result.emplace( along, std::bit_cast<uint32_t>( patch.Mesh.Vertices[i].Position.y ) );
        }
        return result;
    }
}

TEST_CASE( "Terrain hierarchy: signed rectangular forests round trip exact leaf data and bounded roots" )
{
    HierarchyFixture f( true, true ); const auto& a = *f.Archive; std::string error;
    REQUIRE( a.Hierarchical ); CHECK( a.Header.size() == 128 ); CHECK( a.Header[3] == '6' ); CHECK( a.Nodes.size() < 2 * a.Blocks.size() ); Partition( a, a.Roots );
    CHECK( a.Coverage->Heights->GetTiles().empty() ); CHECK( a.Coverage->CollisionTiles.empty() ); CHECK( a.Coverage->Patches.size() == a.Roots.size() * 4 );
    for( size_t i = 0; i < a.Blocks.size(); ++i )
    {
        CHECK( a.Nodes[i].Coordinate == a.Blocks[i].Coordinate ); CHECK( a.Nodes[i].Level == 0 );
        std::vector<float> heights; REQUIRE( TerrainData::ReadStreamHeights( a, a.Blocks[i].Coordinate, heights, error ) ); CHECK( heights == f.Field.FindTile( a.Blocks[i].Coordinate )->Heights );
        std::shared_ptr<const TerrainData::StreamCoverageTile> page; REQUIRE( TerrainData::ReadStreamCoverage( a, a.Blocks[i].Coordinate, page, error ) );
        for( const auto& p : page->Patches ) CHECK( p->Weights == TerrainData::BuildSurfacePatch( *f.Field.GetSurface(), p->Mesh.SampleOrigin, a.Config.PatchQuads ) );
    }
    for( auto root : a.Roots )
    {
        std::shared_ptr<const TerrainData::StreamCoverageTile> page; REQUIRE( TerrainData::ReadHierarchyCoverage( a, root, page, error ) );
        for( const auto& p : page->Patches )
        {
            const uint32_t q = a.Config.PatchQuads >> a.CoverageLod;
            for( uint32_t z = 0; z <= q; ++z ) for( uint32_t x = 0; x <= q; ++x )
            {
                const TerrainData::SampleCoord sample{ p->Mesh.SampleOrigin.X + ( int64_t( x ) << p->BaseLod ), p->Mesh.SampleOrigin.Z + ( int64_t( z ) << p->BaseLod ) }; float h; Vector3 normal;
                REQUIRE( f.Field.GetSample( sample, h ) == TerrainData::QueryStatus::Ready ); REQUIRE( f.Field.GetNormal( sample, normal ) == TerrainData::QueryStatus::Ready );
                CHECK( p->Mesh.Vertices[size_t( z ) * ( q + 1 ) + x].Position.y == h ); CHECK( p->Mesh.Vertices[size_t( z ) * ( q + 1 ) + x].Normal.InternalVector == normal.InternalVector );
            }
        }
    }
}


TEST_CASE( "Terrain hierarchy: incomplete children retain complete balanced parent coverage" )
{
    HierarchyFixture f; const auto& a = *f.Archive; const auto requests = Requests( a, { { -40, 20, -24 }, 1, 32 } );
    const std::set<uint32_t> wanted( requests.begin(), requests.end() ); std::set<uint32_t> loaded( a.Roots.begin(), a.Roots.end() );
    const auto cut = [&]() { return TerrainData::ReadyCoverageCut( a, [&]( uint32_t i ) { return wanted.contains( i ); }, [&]( uint32_t i ) { return loaded.contains( i ); } ); };
    CHECK( cut() == a.Roots ); const auto root = a.Roots.front(); loaded.insert( uint32_t( a.Nodes[root].Children.front() ) ); CHECK( cut() == a.Roots );
    for( auto i : requests )
    {
        loaded.insert( i ); const auto selected = cut(); Partition( a, selected );
        TerrainData::ResidentTerrain data; data.Heights = a.Coverage->Heights;
        for( auto node : selected ) { std::shared_ptr<const TerrainData::StreamCoverageTile> page; std::string error; REQUIRE( TerrainData::ReadHierarchyCoverage( a, node, page, error ) ); data.Patches.insert( data.Patches.end(), page->Patches.begin(), page->Patches.end() ); }
        TerrainData::BuildHierarchyNeighbors( data );
        for( size_t p = 0; p < data.Patches.size(); ++p ) for( auto n = data.NeighborOffsets[p]; n < data.NeighborOffsets[p + 1]; ++n )
            CHECK( std::abs( int( data.Patches[p]->SpanLevel ) - int( data.Patches[data.NeighborLinks[n].Index]->SpanLevel ) ) <= 1 );
    }
    CHECK( cut().size() > a.Roots.size() );
}


TEST_CASE( "Terrain hierarchy: mixed footprints and canonical detail stitch with identical edge vertices" )
{
    HierarchyFixture f; const auto& a = *f.Archive; const auto requests = Requests( a, { { -40, 20, -24 }, 1, 32 } ); const std::set<uint32_t> wanted( requests.begin(), requests.end() );
    const auto cut = TerrainData::ReadyCoverageCut( a, [&]( uint32_t i ) { return wanted.contains( i ); }, []( uint32_t ) { return true; } );
    TerrainData::ResidentTerrain data; data.Heights = a.Coverage->Heights; data.Hierarchical = true; std::string error; bool fine = false, coarse = false;
    for( auto node : cut )
    {
        if( a.Nodes[node].Level == 0 )
        {
            std::shared_ptr<const TerrainData::StreamTile> page; REQUIRE( TerrainData::ReadStreamTile( a, a.Nodes[node].Coordinate, page, error ) ); data.Patches.insert( data.Patches.end(), page->Patches.begin(), page->Patches.end() ); fine = true;
        }
        else { std::shared_ptr<const TerrainData::StreamCoverageTile> page; REQUIRE( TerrainData::ReadHierarchyCoverage( a, node, page, error ) ); data.Patches.insert( data.Patches.end(), page->Patches.begin(), page->Patches.end() ); coarse = true; }
    }
    REQUIRE( fine ); REQUIRE( coarse ); TerrainData::BuildHierarchyNeighbors( data );
    for( int forced : { -1, 0, 2, 5, 10, 30 } )
    {
        std::vector<TerrainData::PatchLodChoice> choices; REQUIRE_MESSAGE( TerrainData::SelectTerrainLods( data, {}, {}, { 2, 0.2f, forced }, {}, choices, error ), error );
        for( size_t i = 0; i < data.Patches.size(); ++i ) for( auto k = data.NeighborOffsets[i]; k < data.NeighborOffsets[i + 1]; ++k )
        {
            const auto link = data.NeighborLinks[k]; const auto& p = *data.Patches[i]; const auto& n = *data.Patches[link.Index]; const bool vertical = link.Edge < 2;
            const int64_t begin = std::max( vertical ? p.Mesh.SampleOrigin.Z : p.Mesh.SampleOrigin.X, vertical ? n.Mesh.SampleOrigin.Z : n.Mesh.SampleOrigin.X );
            const int64_t end = std::min( ( vertical ? p.Mesh.SampleOrigin.Z : p.Mesh.SampleOrigin.X ) + ( int64_t( a.Config.PatchQuads ) << p.SpanLevel ), ( vertical ? n.Mesh.SampleOrigin.Z : n.Mesh.SampleOrigin.X ) + ( int64_t( a.Config.PatchQuads ) << n.SpanLevel ) );
            CHECK( EdgeVertices( p, a.Config.PatchQuads, choices[i], link.Edge, begin, end ) == EdgeVertices( n, a.Config.PatchQuads, choices[link.Index], link.Edge ^ 1, begin, end ) );
        }
    }
}


TEST_CASE( "Terrain hierarchy: world budgets progressively refine and retain old snapshot charges across travel" )
{
    HierarchyFixture f( true ); TerrainData::TerrainStreamCache cache( f.Archive ); TerrainData::TerrainStreamScheduler scheduler;
    TerrainData::StreamWorldLimits limits; limits.CpuBytes = limits.GpuBytes = limits.CollisionBytes = 1024 * 1024; limits.MaxLoads = 1;
    TerrainData::StreamBatch batch; batch.Cache = &cache; batch.Limits.HoldFrames = 0;
    const auto demand = [&]( Vector3 position, TerrainData::TileCoord required ) {
        batch.Demand.clear(); const auto requests = Requests( *f.Archive, { position, 1, 32 } );
        batch.Demand.push_back( { required, true, true, 0 } );
        for( auto id : requests ) batch.Demand.push_back( { f.Archive->Nodes[id].Coordinate, false, false, double( id ), true, false, int32_t( id ) } );
    };
    uint64_t frame = 0;
    const auto step = [&]() {
        cache.WaitForRequest(); scheduler.Tick( ++frame, { &batch, 1 }, limits ); const auto& stats = scheduler.GetStats();
        CHECK( stats.CpuReserved <= limits.CpuBytes ); CHECK( stats.GpuReserved <= limits.GpuBytes ); CHECK( stats.CollisionReserved <= limits.CollisionBytes ); CHECK( stats.Loading <= 1 );
        Partition( *f.Archive, cache.GetData()->CoverageNodes );
    };
    demand( { -40, 20, -24 }, { -3, -2 } ); for( int n = 0; n < 120; ++n ) step();
    CHECK( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().CoverageLoaded > 0 ); CHECK( cache.GetData()->CoverageNodes.size() > f.Archive->Roots.size() );
    REQUIRE( cache.GetData()->Heights->Query( -40, -24 ).Status == TerrainData::QueryStatus::Ready );
    CHECK( cache.GetData()->Heights->Query( 70, 88 ).Status != TerrainData::QueryStatus::Ready ); // Parents never become canonical query data.
    auto pin = cache.GetData(); demand( { 72, 20, 88 }, { 4, 5 } ); for( int n = 0; n < 8; ++n ) step();
    CHECK( cache.GetStats().Retiring > 0 ); CHECK( pin->Heights->Query( -40, -24 ).Status == TerrainData::QueryStatus::Ready );
    pin.reset(); for( int n = 0; n < 140; ++n ) step(); CHECK( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().CoverageEvicted > 0 );
    REQUIRE( cache.GetData()->Heights->Query( 72, 88 ).Status == TerrainData::QueryStatus::Ready );
    batch.Active = false; batch.Demand.clear(); for( int n = 0; n < 8; ++n ) step(); CHECK( cache.CanRetire() );
}


TEST_CASE( "Terrain hierarchy: corrupt optional parents retain roots and canonical reads without retry loops" )
{
    HierarchyFixture f; std::string error; uint32_t parent = uint32_t( f.Archive->Blocks.size() );
    while( parent < f.Archive->Nodes.size() && f.Archive->Nodes[parent].Root == int32_t( parent ) ) ++parent;
    REQUIRE( parent < f.Archive->Nodes.size() ); const auto node = f.Archive->Nodes[parent];
    std::shared_ptr<const TerrainData::StreamCoverageTile> page; REQUIRE( TerrainData::ReadHierarchyCoverage( *f.Archive, parent, page, error ) ); const auto preserved = page;
    { std::fstream file( f.File, std::ios::binary | std::ios::in | std::ios::out ); file.seekp( std::streamoff( node.Offset ) ); file.put( char( 255 ) ); }
    CHECK_FALSE( TerrainData::ReadHierarchyCoverage( *f.Archive, parent, page, error ) ); CHECK( page == preserved );
    std::shared_ptr<const TerrainData::StreamArchive> reopened; REQUIRE( TerrainData::OpenStreamArchive( f.File, reopened, error ) );
    std::vector<float> heights; REQUIRE( TerrainData::ReadStreamHeights( *reopened, node.Coordinate, heights, error ) ); CHECK( heights == f.Field.FindTile( node.Coordinate )->Heights );
    TerrainData::TerrainStreamCache cache( reopened ); TerrainData::TileDemand wanted{ node.Coordinate, false, false, 0, true, false, int32_t( parent ) };
    for( uint64_t frame = 1; frame < 20; ++frame ) { cache.Tick( frame, { &wanted, 1 }, {} ); cache.WaitForRequest(); Partition( *reopened, cache.GetData()->CoverageNodes ); }
    CHECK( cache.GetStats().Failed == 1 ); CHECK_FALSE( cache.GetStats().Loading ); CHECK( cache.GetStats().CollisionReady ); CHECK_FALSE( cache.GetStats().Error.empty() );
    // Root pages are required load-time coverage; corruption rejects a replacement archive.
    { std::fstream file( f.File, std::ios::binary | std::ios::in | std::ios::out ); file.seekp( std::streamoff( reopened->Nodes[reopened->Roots.front()].Offset ) ); file.put( char( 255 ) ); }
    const auto old = reopened; CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, reopened, error ) ); CHECK( reopened == old );
}


TEST_CASE( "Terrain hierarchy: canonical spikes bound parent geometry and failed cooks preserve the old archive" )
{
    HierarchyFixture f; std::string error; const auto original = f.Archive->Header;
    CHECK_FALSE( TerrainData::CookHierarchicalStreamArchive( f.File, f.Field, 123, 3, error ) );
    REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive->Header == original );
    const auto& s = f.Field.GetSettings(); REQUIRE( f.Field.SetSample( { s.MinSample().X + 1, s.MinSample().Z + 1 }, 20.f, error ) );
    REQUIRE( TerrainData::CookHierarchicalStreamArchive( f.File, f.Field, 123, 1, error ) ); REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) );
    bool bounds = false, envelope = false;
    for( const auto& p : f.Archive->Coverage->Patches ) { bounds |= p->Mesh.Bounds.Max.y == 20.f; envelope |= p->Mesh.Lods.front().MaxError >= 19.f; }
    CHECK( bounds ); CHECK( envelope ); CHECK( f.Archive->Header != original );
    std::vector<float> heights; REQUIRE( TerrainData::ReadStreamHeights( *f.Archive, s.MinTile, heights, error ) ); CHECK( heights[s.TileQuads + 2] == 20.f );
}


TEST_CASE( "Terrain hierarchy: forged checksums cannot bypass directory topology or allocation limits" )
{
    HierarchyFixture f; std::string error;
    std::ifstream input( f.File, std::ios::binary ); const std::vector<uint8_t> valid( ( std::istreambuf_iterator<char>( input ) ), {} ); input.close();
    const size_t directory = 128 + f.Archive->Blocks.size() * 64, parent = directory + f.Archive->Blocks.size() * 64;
    const auto hash = []( std::span<const uint8_t> bytes ) { uint64_t h = 14695981039346656037ull; for( auto b : bytes ) h = ( h ^ b ) * 1099511628211ull; return h; };
    const auto put = []( auto& bytes, size_t offset, uint64_t value, unsigned count = 8 ) { for( unsigned i = 0; i < count; ++i ) bytes[offset + i] = uint8_t( value >> ( i * 8 ) ); };
    const auto preserved = f.Archive;
    for( const auto [offset, value, width] : std::initializer_list<std::tuple<size_t, uint64_t, unsigned>>{
        { directory, 12345, 4 }, { parent + 8, 31, 4 }, { parent + 12, f.Archive->Nodes.size() - 1, 4 },
        { parent + 16, f.Archive->Blocks.size(), 4 }, { parent + 32, UINT64_MAX, 8 }, { parent + 40, UINT64_MAX, 8 },
        { directory + 48, f.Archive->Nodes.front().Checksum ^ 1, 8 }, { parent + 56, 0x7fc00000, 4 }, { parent + 56, 0, 4 },
        { 96, UINT64_MAX, 8 }, { 112, UINT64_MAX, 8 } } )
    {
        auto bytes = valid; put( bytes, offset, value, width );
        put( bytes, 104, hash( std::span( bytes ).subspan( directory, f.Archive->Nodes.size() * 64 ) ) );
        put( bytes, 120, hash( std::span( bytes ).first( 120 ) ) );
        { std::ofstream output( f.File, std::ios::binary | std::ios::trunc ); output.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() ); REQUIRE( output.good() ); }
        CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive == preserved ); CHECK_FALSE( error.empty() );
    }
    { std::ofstream output( f.File, std::ios::binary | std::ios::trunc ); output.write( reinterpret_cast<const char*>( valid.data() ), 127 ); }
    CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive == preserved );
}


TEST_CASE( "Terrain hierarchy: camera unions refine both ends and reject invalid requests transactionally" )
{
    HierarchyFixture f; const auto& a = *f.Archive; std::string error;
    const TerrainData::CoverageView views[]{ { { -47, 10, -31 }, 1, 8 }, { { 79, 10, 95 }, 1, 8 } };
    std::vector<uint32_t> requests; REQUIRE( TerrainData::SelectCoverageRequests( a, views, 64, requests, error ) );
    const std::set<uint32_t> wanted( requests.begin(), requests.end() );
    const auto cut = TerrainData::ReadyCoverageCut( a, [&]( uint32_t id ) { return wanted.contains( id ); }, []( uint32_t ) { return true; } );
    Partition( a, cut ); CHECK( cut.size() <= 64 );
    CHECK( std::find( cut.begin(), cut.end(), uint32_t( a.TileIndex( { -3, -2 } ) ) ) != cut.end() );
    CHECK( std::find( cut.begin(), cut.end(), uint32_t( a.TileIndex( { 4, 5 } ) ) ) != cut.end() );
    const auto preserved = requests;
    auto invalid = views[0]; invalid.Position.x = std::numeric_limits<float>::infinity();
    CHECK_FALSE( TerrainData::SelectCoverageRequests( a, { &invalid, 1 }, 64, requests, error ) ); CHECK( requests == preserved );
    CHECK_FALSE( TerrainData::SelectCoverageRequests( a, views, 0, requests, error ) ); CHECK( requests == preserved );
    REQUIRE( TerrainData::SelectCoverageRequests( a, {}, 64, requests, error ) ); CHECK( requests == a.Roots );
    auto settings = a.Config; settings.TilesX = 0; auto nodes = a.Nodes; auto roots = a.Roots;
    CHECK_FALSE( TerrainData::BuildCoverageHierarchy( settings, nodes, roots, error ) ); CHECK( nodes.size() == a.Nodes.size() ); CHECK( roots == a.Roots );
    settings = a.Config; settings.TilesX = settings.TilesZ = 257;
    CHECK_FALSE( TerrainData::BuildCoverageHierarchy( settings, nodes, roots, error ) ); CHECK( nodes.size() == a.Nodes.size() ); CHECK( roots == a.Roots );
}


TEST_CASE( "Terrain hierarchy: resident root paint is reserved and cancelled refinements cannot publish" )
{
    HierarchyFixture plain, painted( true );
    TerrainData::TerrainStreamCache a( plain.Archive ), cache( painted.Archive );
    uint64_t weights = 0; for( const auto& p : painted.Archive->Coverage->Patches ) weights += p->Weights.size() * 4;
    CHECK( weights > 0 ); CHECK( cache.GetStats().CpuReserved >= a.GetStats().CpuReserved + weights * 6 );
    CHECK( cache.GetStats().GpuReserved >= a.GetStats().GpuReserved + weights * 4 );
    const auto id = uint32_t( painted.Archive->Nodes[painted.Archive->Roots.front()].Children.front() );
    const TerrainData::TileDemand request{ painted.Archive->Nodes[id].Coordinate, false, false, 0, true, false, int32_t( id ) };
    TerrainData::StreamLimits limits; limits.HoldFrames = 0;
    cache.Tick( 1, { &request, 1 }, limits ); REQUIRE( cache.GetStats().Loading ); cache.InvalidatePending(); cache.WaitForRequest();
    for( uint64_t frame = 2; frame < 12; ++frame ) cache.Tick( frame, {}, limits );
    CHECK( cache.GetStats().Cancelled > 0 ); CHECK_FALSE( cache.GetStats().Loading ); CHECK( cache.GetStats().CoverageResident == 0 );
    CHECK( cache.GetData()->CoverageNodes == painted.Archive->Roots ); CHECK( cache.CanRetire() );
}
