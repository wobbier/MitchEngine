#include "PCH.h"
#include "ResidentTerrain.h"
#include <cmath>
#include <stdexcept>

namespace TerrainData
{
    bool IsTranslationOnly( const Matrix4& InWorld )
    {
        const auto& m = InWorld.GetInternalMatrix();
        for( int column = 0; column < 4; ++column ) for( int row = 0; row < 4; ++row )
        {
            if( !std::isfinite( m[column][row] ) ) return false;
            if( column == 3 && row < 3 ) continue;
            if( std::abs( m[column][row] - ( column == row ? 1.f : 0.f ) ) > 1e-5f ) return false;
        }
        return true;
    }

    bool BuildResidentTerrain( const Heightfield& InField, uint64_t InAssetGUID, std::shared_ptr<const ResidentTerrain>& OutData, std::string& OutError )
    {
        OutError.clear();
        try
        {
            const auto& s = InField.GetSettings();
            if( !InAssetGUID || InField.GetTiles().size() != static_cast<uint64_t>( s.TilesX ) * s.TilesZ )
                throw std::runtime_error( "Resident terrain requires a nonzero GUID and all source tiles" );
            auto data = std::make_shared<ResidentTerrain>();
            data->AssetGUID = InAssetGUID;
            data->Heights = std::make_shared<Heightfield>( InField );
            const auto max = s.MaxSample(), min = s.MinSample();
            for( int64_t coordinate : { min.X, min.Z, max.X, max.Z } )
            {
                const double position = coordinate * static_cast<double>( s.SampleSpacing );
                const float a = static_cast<float>( position ), b = static_cast<float>( position + s.SampleSpacing );
                if( !std::isfinite( a ) || !std::isfinite( b ) || a == b )
                    throw std::runtime_error( "Terrain coordinates exceed the resident renderer's Float32 precision" );
            }
            for( const auto& [coord, tile] : InField.GetTiles() )
            {
                for( uint32_t z = 0; z < s.TileQuads / s.PatchQuads; ++z ) for( uint32_t x = 0; x < s.TileQuads / s.PatchQuads; ++x )
                {
                    ResidentPatch patch;
                    if( !BuildPatch( InField, coord, x, z, patch.Mesh, OutError ) ) throw std::runtime_error( OutError );
                    patch.Offset = Vector3( static_cast<float>( patch.Mesh.SampleOrigin.X * static_cast<double>( s.SampleSpacing ) ), 0.f,
                        static_cast<float>( patch.Mesh.SampleOrigin.Z * static_cast<double>( s.SampleSpacing ) ) );
                    AABB bounds = patch.Mesh.Bounds;
                    bounds.Min += patch.Offset; bounds.Max += patch.Offset;
                    data->Bounds.Encapsulate( bounds );
                    data->Patches.push_back( std::move( patch ) );
                }
                CollisionTile collision;
                collision.Coordinate = coord;
                const uint32_t side = s.TileQuads + 1;
                collision.Positions.reserve( tile.Heights.size() );
                for( uint32_t z = 0; z <= s.TileQuads; ++z ) for( uint32_t x = 0; x <= s.TileQuads; ++x )
                    collision.Positions.emplace_back( static_cast<float>( ( static_cast<int64_t>( coord.X ) * s.TileQuads + x ) * static_cast<double>( s.SampleSpacing ) ),
                        tile.Heights[static_cast<size_t>( z ) * side + x],
                        static_cast<float>( ( static_cast<int64_t>( coord.Z ) * s.TileQuads + z ) * static_cast<double>( s.SampleSpacing ) ) );
                collision.Indices.reserve( static_cast<size_t>( s.TileQuads ) * s.TileQuads * 6 );
                for( uint32_t z = 0; z < s.TileQuads; ++z ) for( uint32_t x = 0; x < s.TileQuads; ++x )
                {
                    const uint32_t a = z * side + x, b = a + side;
                    collision.Indices.insert( collision.Indices.end(), { a, b, a + 1, a + 1, b, b + 1 } );
                }
                data->CollisionTiles.push_back( std::move( collision ) );
            }
            OutData = std::move( data );
            return true;
        }
        catch( const std::exception& e ) { OutError = e.what(); return false; }
    }
}
