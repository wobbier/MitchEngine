#include "PCH.h"
#include "TerrainNavGeometry.h"
#include "Navigation/NavMeshBuilder.h"
#include "Terrain/TerrainStreamArchive.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <climits>
#include <stdexcept>

namespace
{
    constexpr uint64_t kTriangleLimit = 4000000;

    uint64_t HashCombine( uint64_t seed, uint64_t value )
    {
        for( unsigned i = 0; i < 8; ++i ) seed = ( seed ^ uint8_t( value >> ( i * 8 ) ) ) * 1099511628211ull;
        return seed;
    }


    void Require( bool condition, const char* message )
    {
        if( !condition ) throw std::runtime_error( message );
    }


    uint64_t Triangles( const NavTerrainSource::Region& region )
    {
        Require( region.X1 > region.X0 && region.Z1 > region.Z0, "Invalid terrain navigation region" );
        return uint64_t( region.X1 - region.X0 ) * ( region.Z1 - region.Z0 ) * 2;
    }
}


bool CaptureNavTerrain( std::shared_ptr<const TerrainData::Heightfield> heights,
    std::shared_ptr<const TerrainData::StreamArchive> archive, const Vector3& translation,
    uint8_t area, const AABB& bounds, const NavBuildSettings& settings, NavTerrainSource& out, std::string& error )
{
    try
    {
        Require( heights != nullptr, "Terrain heights are not ready" );
        const auto& s = heights->GetSettings();
        const float cs = std::max( settings.GetCellSize(), 0.01f );
        Require( std::isfinite( cs ) && cs > 0 && std::isfinite( settings.AgentRadius ) && settings.AgentRadius >= 0 && settings.TileSize > 0,
            "Invalid navigation cell size, agent radius or tile size" );
        Require( std::isfinite( translation.x ) && std::isfinite( translation.y ) && std::isfinite( translation.z ), "Invalid terrain navigation translation" );
        if( archive ) Require( archive->Config.TileQuads == s.TileQuads && archive->Config.SampleSpacing == s.SampleSpacing &&
            archive->Config.MinTile == s.MinTile && archive->Config.TilesX == s.TilesX && archive->Config.TilesZ == s.TilesZ, "Terrain navigation archive/settings mismatch" );
        NavTerrainSource source; source.Heights = std::move( heights ); source.Archive = std::move( archive ); source.Translation = translation; source.Area = area;
        auto lo = s.MinSample(), hi = s.MaxSample();
        if( bounds.IsValid() )
        {
            const double margin = ( std::ceil( settings.AgentRadius / double( cs ) ) + 3 + std::clamp( settings.TileSize, 16, 1024 ) ) * cs;
            const auto clip = [&]( float point, float origin, double padding, int64_t minimum, int64_t maximum, bool upper ) {
                const double sample = ( point - double( origin ) + padding ) / s.SampleSpacing;
                Require( std::isfinite( sample ), "Invalid terrain navigation collection bounds" );
                return static_cast<int64_t>( std::clamp( upper ? std::ceil( sample ) : std::floor( sample ), double( minimum ), double( maximum ) ) );
            };
            lo.X = clip( bounds.Min.x, translation.x, -margin, lo.X, hi.X, false );
            lo.Z = clip( bounds.Min.z, translation.z, -margin, lo.Z, hi.Z, false );
            hi.X = clip( bounds.Max.x, translation.x, margin, s.MinSample().X, hi.X, true );
            hi.Z = clip( bounds.Max.z, translation.z, margin, s.MinSample().Z, hi.Z, true );
        }
        uint64_t hash = HashCombine( 14695981039346656037ull, s.TileQuads ); hash = HashCombine( hash, std::bit_cast<uint32_t>( s.SampleSpacing ) );
        hash = HashCombine( hash, std::bit_cast<uint32_t>( translation.x ) ); hash = HashCombine( hash, std::bit_cast<uint32_t>( translation.y ) );
        hash = HashCombine( hash, std::bit_cast<uint32_t>( translation.z ) ); hash = HashCombine( hash, area );
        if( lo.X < hi.X && lo.Z < hi.Z )
        {
            const auto tx0 = TerrainData::FloorDivide( lo.X, s.TileQuads ), tx1 = TerrainData::FloorDivide( hi.X - 1, s.TileQuads );
            const auto tz0 = TerrainData::FloorDivide( lo.Z, s.TileQuads ), tz1 = TerrainData::FloorDivide( hi.Z - 1, s.TileQuads );
            uint64_t count = 0;
            for( int64_t tx = tx0; tx <= tx1; ++tx ) for( int64_t tz = tz0; tz <= tz1; ++tz )
            {
                NavTerrainSource::Region region; region.Tile = { static_cast<int32_t>( tx ), static_cast<int32_t>( tz ) };
                const int64_t x = tx * s.TileQuads, z = tz * s.TileQuads;
                region.X0 = static_cast<uint32_t>( std::max( lo.X - x, int64_t( 0 ) ) ); region.Z0 = static_cast<uint32_t>( std::max( lo.Z - z, int64_t( 0 ) ) );
                region.X1 = static_cast<uint32_t>( std::min( hi.X - x, int64_t( s.TileQuads ) ) ); region.Z1 = static_cast<uint32_t>( std::min( hi.Z - z, int64_t( s.TileQuads ) ) );
                count += Triangles( region );
                Require( count <= kTriangleLimit, "Terrain navigation exceeds four million triangles; use a collection volume or split the surfaces" );
                source.Regions.push_back( region );
            }
            // Validate/fingerprint only selected data. No canonical stream page is read here.
            for( const auto& r : source.Regions )
            {
                hash = HashCombine( hash, uint32_t( r.Tile.X ) ); hash = HashCombine( hash, uint32_t( r.Tile.Z ) );
                for( uint32_t v : { r.X0, r.Z0, r.X1, r.Z1 } ) hash = HashCombine( hash, v );
                if( source.Archive ) hash = HashCombine( hash, source.Archive->Blocks.at( source.Archive->TileIndex( r.Tile ) ).Checksum );
                else
                {
                    const auto* tile = source.Heights->FindTile( r.Tile ); Require( tile != nullptr, "Canonical terrain tile is unavailable for navigation" );
                    for( uint32_t z = r.Z0; z <= r.Z1; ++z ) for( uint32_t x = r.X0; x <= r.X1; ++x )
                        hash = HashCombine( hash, std::bit_cast<uint32_t>( tile->Heights.at( size_t( z ) * ( s.TileQuads + 1 ) + x ) ) );
                }
            }
        }
        source.Fingerprint = hash; out = std::move( source ); error.clear(); return true;
    }
    catch( const std::exception& e ) { error = e.what(); return false; }
}


bool AppendNavTerrainGeometry( NavBuildInput& input, std::string& error, std::atomic<bool>* cancel )
{
    try
    {
        uint64_t triangles = 0;
        for( const auto& source : input.Terrains )
        {
            Require( source.Heights != nullptr, "Terrain navigation source is missing" );
            const auto& s = source.Heights->GetSettings();
            for( const auto& region : source.Regions )
            {
                Require( s.Contains( region.Tile ) && region.X1 <= s.TileQuads && region.Z1 <= s.TileQuads, "Invalid terrain navigation region" );
                triangles += Triangles( region );
                Require( triangles <= kTriangleLimit, "Terrain navigation exceeds four million triangles; use a collection volume or split the surfaces" );
            }
        }
        for( const auto& source : input.Terrains )
        {
            const auto& s = source.Heights->GetSettings();
            for( const auto& r : source.Regions )
            {
                Require( !cancel || !cancel->load(), "cancelled" );
                std::vector<float> page;
                if( source.Archive && !TerrainData::ReadStreamHeights( *source.Archive, r.Tile, page, error ) ) return false;
                const auto* resident = source.Archive ? nullptr : source.Heights->FindTile( r.Tile );
                Require( source.Archive || resident, "Canonical terrain tile is unavailable for navigation" );
                const auto& heights = source.Archive ? page : resident->Heights;
                const uint32_t width = r.X1 - r.X0 + 1;
                Require( input.Vertices.size() + uint64_t( width ) * ( r.Z1 - r.Z0 + 1 ) < INT_MAX, "Terrain navigation vertex index overflow" );
                const int base = static_cast<int>( input.Vertices.size() );
                for( uint32_t z = r.Z0; z <= r.Z1; ++z )
                {
                    Require( !cancel || !cancel->load(), "cancelled" );
                    for( uint32_t x = r.X0; x <= r.X1; ++x )
                    {
                        const Vector3 point( float( ( int64_t( r.Tile.X ) * s.TileQuads + x ) * double( s.SampleSpacing ) ) + source.Translation.x,
                            heights.at( size_t( z ) * ( s.TileQuads + 1 ) + x ) + source.Translation.y,
                            float( ( int64_t( r.Tile.Z ) * s.TileQuads + z ) * double( s.SampleSpacing ) ) + source.Translation.z );
                        Require( std::isfinite( point.x ) && std::isfinite( point.y ) && std::isfinite( point.z ), "Nonfinite terrain navigation vertex" );
                        input.Vertices.push_back( point );
                    }
                }
                for( uint32_t z = 0; z < r.Z1 - r.Z0; ++z ) for( uint32_t x = 0; x + 1 < width; ++x )
                {
                    const int a = base + static_cast<int>( z * width + x ), b = a + static_cast<int>( width );
                    input.Indices.insert( input.Indices.end(), { a, b, a + 1, a + 1, b, b + 1 } );
                    input.Areas.insert( input.Areas.end(), 2, source.Area );
                }
            }
        }
        input.Terrains.clear(); error.clear(); return true;
    }
    catch( const std::exception& e ) { error = e.what(); return false; }
}
