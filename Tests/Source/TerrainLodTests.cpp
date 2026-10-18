#include <doctest/doctest.h>
#include "Terrain/TerrainLod.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace
{
    using Edge = std::pair<uint32_t, uint32_t>;
    std::shared_ptr<TerrainData::ResidentTerrain> SelectionField()
    {
        TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 4; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
        TerrainData::Heightfield field( s ); std::string error;
        for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) REQUIRE( field.CreateFlatTile( { x, z }, 0, error ) );
        std::shared_ptr<const TerrainData::ResidentTerrain> prepared;
        REQUIRE( TerrainData::BuildResidentTerrain( field, 123, prepared, error ) );
        auto data = std::make_shared<TerrainData::ResidentTerrain>( *prepared );
        // Deliberately conservative synthetic errors make selection thresholds deterministic.
        for( auto& patch : data->Patches )
        {
            auto replacement = std::make_shared<TerrainData::ResidentPatch>( *patch );
            replacement->Mesh.Lods[1].MaxError = 1; replacement->Mesh.Lods[2].MaxError = 4; patch = replacement;
        }
        return data;
    }
}

TEST_CASE( "Terrain LOD: all stitch masks preserve winding, coverage, shared edges and parent diagonals" )
{
    std::string error;
    for( uint32_t q : { 2u, 4u, 8u, 16u, 32u } ) for( uint32_t stride = 1; stride < q; stride *= 2 ) for( uint8_t mask = 0; mask < 16; ++mask )
    {
        INFO( q, ", ", stride, ", ", int( mask ) );
        std::vector<uint32_t> indices; REQUIRE( TerrainData::BuildStitchedIndices( q, stride, mask, indices, error ) );
        std::map<Edge, int> edges; int64_t total = 0;
        const auto x = [q]( uint32_t i ) { return int64_t( i % ( q + 1 ) ); };
        const auto z = [q]( uint32_t i ) { return int64_t( i / ( q + 1 ) ); };
        for( size_t i = 0; i < indices.size(); i += 3 )
        {
            const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
            CHECK( std::max( { a, b, c } ) < ( q + 1 ) * ( q + 1 ) );
            const int64_t area = ( x( b ) - x( a ) ) * ( z( c ) - z( a ) ) - ( z( b ) - z( a ) ) * ( x( c ) - x( a ) );
            REQUIRE( area < 0 ); total -= area;
            for( const auto pair : { Edge{ a, b }, Edge{ b, c }, Edge{ c, a } } ) ++edges[std::minmax( pair.first, pair.second )];
            const int64_t step = 2 * stride;
            const int64_t ox = std::min( std::min( { x( a ), x( b ), x( c ) } ) / step * step, q - step );
            const int64_t oz = std::min( std::min( { z( a ), z( b ), z( c ) } ) / step * step, q - step );
            int64_t minSum = 4 * step, maxSum = 0;
            for( auto vertex : { a, b, c } )
            {
                CHECK( x( vertex ) >= ox ); CHECK( x( vertex ) <= ox + step ); CHECK( z( vertex ) >= oz ); CHECK( z( vertex ) <= oz + step );
                const auto sum = x( vertex ) - ox + z( vertex ) - oz; minSum = std::min( minSum, sum ); maxSum = std::max( maxSum, sum );
            }
            CHECK( ( maxSum <= step || minSum >= step ) );
        }
        CHECK( total == 2 * q * q );
        for( const auto& [edge, count] : edges )
        {
            const auto a = edge.first, b = edge.second;
            if( count == 2 ) continue;
            REQUIRE( count == 1 );
            int bit = 0; int64_t length = 0;
            if( x( a ) == 0 && x( b ) == 0 ) { bit = 1; length = std::abs( z( a ) - z( b ) ); }
            if( x( a ) == q && x( b ) == q ) { bit = 2; length = std::abs( z( a ) - z( b ) ); }
            if( z( a ) == 0 && z( b ) == 0 ) { bit = 4; length = std::abs( x( a ) - x( b ) ); }
            if( z( a ) == q && z( b ) == q ) { bit = 8; length = std::abs( x( a ) - x( b ) ); }
            REQUIRE( bit != 0 ); CHECK( length == stride * ( mask & bit ? 2 : 1 ) );
        }
    }
    std::vector<uint32_t> original{ 123 };
    CHECK_FALSE( TerrainData::BuildStitchedIndices( 3, 1, 0, original, error ) );
    CHECK_FALSE( TerrainData::BuildStitchedIndices( 8, 3, 0, original, error ) );
    CHECK_FALSE( TerrainData::BuildStitchedIndices( 8, 8, 1, original, error ) );
    CHECK_FALSE( TerrainData::BuildStitchedIndices( 8, 1, 16, original, error ) ); CHECK( original == std::vector<uint32_t>{ 123 } );
}

TEST_CASE( "Terrain LOD: signed cross-tile neighbors balance and stitch within the pixel budget" )
{
    auto data = SelectionField(); TerrainData::LodView view; view.Position = Vector3( 0, 0, -20 ); view.PixelsX = view.PixelsY = 100;
    TerrainData::LodSettings settings; settings.PixelError = 10;
    std::vector<TerrainData::PatchLodChoice> choices; std::string error;
    REQUIRE( TerrainData::SelectTerrainLods( *data, {}, { &view, 1 }, settings, {}, choices, error ) );
    size_t stitched = 0, fine = 0, coarse = 0;
    const int opposite[] = { 1, 0, 3, 2 };
    for( size_t i = 0; i < choices.size(); ++i )
    {
        CHECK( choices[i].PixelError <= settings.PixelError + 1e-8 );
        fine += choices[i].Level == 0; coarse += choices[i].Level > 0; stitched += choices[i].Edges != 0;
        for( int edge = 0; edge < 4; ++edge ) if( const auto n = data->PatchNeighbors[i][edge]; n >= 0 )
        {
            CHECK( data->PatchNeighbors[n][opposite[edge]] == static_cast<int32_t>( i ) );
            CHECK( std::abs( int( choices[i].Level ) - int( choices[n].Level ) ) <= 1 );
            CHECK( bool( choices[i].Edges & ( 1 << edge ) ) == ( choices[n].Level > choices[i].Level ) );
        }
    }
    CHECK( fine > 0 ); CHECK( coarse > 0 ); CHECK( stitched > 0 );
    const auto before = choices; settings.ForcedLevel = 2;
    REQUIRE( TerrainData::SelectTerrainLods( *data, {}, { &view, 1 }, settings, before, choices, error ) );
    for( const auto& choice : choices ) { CHECK( choice.Level == 2 ); CHECK( choice.Edges == 0 ); }
    settings.PixelError = std::numeric_limits<float>::quiet_NaN(); const auto retained = choices;
    CHECK_FALSE( TerrainData::SelectTerrainLods( *data, {}, {}, settings, {}, choices, error ) ); CHECK( choices.size() == retained.size() );
}

TEST_CASE( "Terrain LOD: orthographic hysteresis, translation and multiple cameras retain requested detail" )
{
    auto data = SelectionField(); TerrainData::LodView view; view.Position = Vector3( 0, 0, -100 ); view.Orthographic = true;
    TerrainData::LodSettings settings; settings.PixelError = 4.5f;
    std::vector<TerrainData::PatchLodChoice> choices, previous( data->Patches.size() ); std::string error;
    for( auto& choice : previous ) choice.Level = 1;
    REQUIRE( TerrainData::SelectTerrainLods( *data, {}, { &view, 1 }, settings, {}, choices, error ) ); CHECK( choices.front().Level == 2 );
    REQUIRE( TerrainData::SelectTerrainLods( *data, {}, { &view, 1 }, settings, previous, choices, error ) ); CHECK( choices.front().Level == 1 );
    settings.PixelError = 3.9f;
    for( auto& choice : previous ) choice.Level = 2;
    REQUIRE( TerrainData::SelectTerrainLods( *data, {}, { &view, 1 }, settings, previous, choices, error ) ); CHECK( choices.front().Level == 1 );
    TerrainData::LodView views[] = { view, view }; views[1].PixelsY = 100;
    REQUIRE( TerrainData::SelectTerrainLods( *data, {}, views, settings, previous, choices, error ) );
    for( const auto& choice : choices ) CHECK( choice.Level == 0 );
    views[1].ForceFine = true;
    REQUIRE( TerrainData::SelectTerrainLods( *data, {}, views, settings, {}, choices, error ) ); CHECK( choices.front().Level == 0 );
    view.Position += Vector3( 100, 20, 30 );
    REQUIRE( TerrainData::SelectTerrainLods( *data, Vector3( 100, 20, 30 ), { &view, 1 }, settings, previous, choices, error ) ); CHECK( choices.front().Level == 1 );
}

TEST_CASE( "Terrain LOD: perspective pixel error bounds projection with changing camera depth" )
{
    AABB box; box.Min = Vector3( -5, -4, 20 ); box.Max = Vector3( 5, 4, 40 );
    TerrainData::LodView view; view.Position = Vector3( 2, 10, 0 ); view.Forward = Vector3( 0.2f, -0.6f, 1 ); view.PixelsX = 900; view.PixelsY = 800;
    const double scale = TerrainData::VerticalPixelScale( box, view ); REQUIRE( std::isfinite( scale ) );
    const auto f = glm::normalize( glm::dvec3( view.Forward.InternalVector ) );
    const auto r = glm::normalize( glm::cross( glm::dvec3( view.Up.InternalVector ), f ) ), u = glm::cross( f, r );
    const auto project = [&]( const glm::dvec3& p ) {
        const auto v = p - glm::dvec3( view.Position.InternalVector );
        return glm::dvec2( view.PixelsX * glm::dot( v, r ), view.PixelsY * glm::dot( v, u ) ) / glm::dot( v, f );
    };
    for( double x : { -5., 0., 5. } ) for( double z : { 20., 25., 40. } ) for( double y : { -4., -1., 3. } )
        CHECK( glm::length( project( { x, y + 1, z } ) - project( { x, y, z } ) ) <= scale + 1e-9 );
    view.Position = Vector3( 0, 0, 30 ); CHECK( std::isinf( TerrainData::VerticalPixelScale( box, view ) ) );
}

TEST_CASE( "Terrain LOD: stitched height error stays inside its next-level bound on a curved surface" )
{
    TerrainData::Settings s; s.TileQuads = s.PatchQuads = 8;
    TerrainData::Heightfield field( s ); std::string error;
    REQUIRE( field.CreateFlatTile( { 0, 0 }, 0, error ) );
    for( int z = 0; z <= 8; ++z ) for( int x = 0; x <= 8; ++x )
        REQUIRE( field.SetSample( { x, z }, static_cast<float>( 3 * std::sin( x * 0.7 + z * 0.3 ) + 1.7 * std::cos( z * 0.6 ) ), error ) );
    TerrainData::PatchMesh mesh; REQUIRE( TerrainData::BuildPatch( field, { 0, 0 }, 0, 0, mesh, error ) );
    for( size_t level = 0; level + 1 < mesh.Lods.size(); ++level ) for( uint8_t mask = 0; mask < 16; ++mask )
    {
        std::vector<uint32_t> indices; REQUIRE( TerrainData::BuildStitchedIndices( 8, 1u << level, mask, indices, error ) );
        const double bound = mask ? 2.0 * mesh.Lods[level + 1].MaxError : mesh.Lods[level].MaxError;
        for( int iz = 0; iz < 20; ++iz ) for( int ix = 0; ix < 20; ++ix )
        {
            const double x = ( ix + 0.31 ) * 0.4, z = ( iz + 0.23 ) * 0.4;
            bool found = false;
            for( size_t triangle = 0; triangle < indices.size(); triangle += 3 )
            {
                const auto& a = mesh.Vertices[indices[triangle]].Position;
                const auto& b = mesh.Vertices[indices[triangle + 1]].Position;
                const auto& c = mesh.Vertices[indices[triangle + 2]].Position;
                const double det = ( b.z - c.z ) * ( a.x - c.x ) + ( c.x - b.x ) * ( a.z - c.z );
                const double u = ( ( b.z - c.z ) * ( x - c.x ) + ( c.x - b.x ) * ( z - c.z ) ) / det;
                const double v = ( ( c.z - a.z ) * ( x - c.x ) + ( a.x - c.x ) * ( z - c.z ) ) / det;
                if( u < -1e-8 || v < -1e-8 || u + v > 1 + 1e-8 ) continue;
                const double height = u * a.y + v * b.y + ( 1 - u - v ) * c.y;
                CHECK( std::abs( height - field.Query( x, z ).Height ) <= bound + 2e-5 ); found = true; break;
            }
            REQUIRE( found );
        }
    }
}
