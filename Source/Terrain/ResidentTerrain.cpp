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
        return UpdateResidentTerrain( InField, InAssetGUID, nullptr, OutData, OutError );
    }


    bool UpdateResidentTerrain( const Heightfield& InField, uint64_t InAssetGUID, const std::shared_ptr<const ResidentTerrain>& InPrevious,
        std::shared_ptr<const ResidentTerrain>& OutData, std::string& OutError, const std::atomic_bool* InCancel )
    {
        OPTICK_EVENT( "TerrainData::UpdateResidentTerrain" );
        OutError.clear();
        try
        {
            const auto checkCancelled = [&]() { if( InCancel && InCancel->load( std::memory_order_relaxed ) ) throw std::runtime_error( "Terrain preview cancelled" ); };
            checkCancelled();
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
            const auto compatible = [&]() {
                if( !InPrevious || !InPrevious->Heights || InPrevious->AssetGUID != InAssetGUID ) return false;
                const auto& old = InPrevious->Heights->GetSettings();
                const uint64_t patches = static_cast<uint64_t>( s.TilesX ) * s.TilesZ * ( s.TileQuads / s.PatchQuads ) * ( s.TileQuads / s.PatchQuads );
                return old.TileQuads == s.TileQuads && old.PatchQuads == s.PatchQuads && old.MinTile == s.MinTile && old.TilesX == s.TilesX && old.TilesZ == s.TilesZ &&
                    old.SampleSpacing == s.SampleSpacing && old.HeightBase == s.HeightBase && old.HeightRange == s.HeightRange &&
                    InPrevious->CollisionTiles.size() == InField.GetTiles().size() && InPrevious->Patches.size() == patches;
            }();
            // Per-tile changed bounds include exactly the sample changes, then render tests add the normal halo.
            std::vector<SampleRect> changes;
            std::set<TileCoord> collisionChanges;
            if( compatible ) for( const auto& [coord, tile] : InField.GetTiles() )
            {
                checkCancelled();
                const auto* old = InPrevious->Heights->FindTile( coord );
                if( !old || old->Heights.size() != tile.Heights.size() ) throw std::runtime_error( "Incompatible previous terrain samples" );
                SampleRect changed;
                const uint32_t side = s.TileQuads + 1;
                for( size_t i = 0; i < tile.Heights.size(); ++i ) if( tile.Heights[i] != old->Heights[i] )
                    changed.Include( { static_cast<int64_t>( coord.X ) * s.TileQuads + static_cast<int64_t>( i % side ),
                        static_cast<int64_t>( coord.Z ) * s.TileQuads + static_cast<int64_t>( i / side ) } );
                if( !changed.IsEmpty() ) { changes.push_back( changed ); collisionChanges.insert( coord ); }
            }
            size_t patchIndex = 0, tileIndex = 0;
            for( const auto& [coord, tile] : InField.GetTiles() )
            {
                for( uint32_t z = 0; z < s.TileQuads / s.PatchQuads; ++z ) for( uint32_t x = 0; x < s.TileQuads / s.PatchQuads; ++x )
                {
                    checkCancelled();
                    const SampleCoord origin{ static_cast<int64_t>( coord.X ) * s.TileQuads + x * s.PatchQuads,
                        static_cast<int64_t>( coord.Z ) * s.TileQuads + z * s.PatchQuads };
                    bool rebuild = !compatible;
                    for( const auto& changed : changes )
                        if( changed.Max.X >= origin.X - 1 && changed.Min.X <= origin.X + s.PatchQuads + 1 &&
                            changed.Max.Z >= origin.Z - 1 && changed.Min.Z <= origin.Z + s.PatchQuads + 1 ) { rebuild = true; break; }
                    std::shared_ptr<const ResidentPatch> result;
                    if( !rebuild ) result = InPrevious->Patches[patchIndex];
                    else
                    {
                        auto patch = std::make_shared<ResidentPatch>();
                        if( !BuildPatch( InField, coord, x, z, patch->Mesh, OutError, InCancel ) ) throw std::runtime_error( OutError );
                        patch->Offset = Vector3( static_cast<float>( origin.X * static_cast<double>( s.SampleSpacing ) ), 0.f,
                            static_cast<float>( origin.Z * static_cast<double>( s.SampleSpacing ) ) );
                        result = std::move( patch ); ++data->BuiltPatches;
                    }
                    if( !result ) throw std::runtime_error( "Missing previous terrain patch" );
                    AABB bounds = result->Mesh.Bounds;
                    bounds.Min += result->Offset; bounds.Max += result->Offset;
                    data->Bounds.Encapsulate( bounds );
                    data->Patches.push_back( std::move( result ) ); ++patchIndex;
                }
                if( compatible && !collisionChanges.contains( coord ) )
                {
                    if( !InPrevious->CollisionTiles[tileIndex] ) throw std::runtime_error( "Missing previous terrain collision tile" );
                    data->CollisionTiles.push_back( InPrevious->CollisionTiles[tileIndex++] ); continue;
                }
                checkCancelled();
                auto collision = std::make_shared<CollisionTile>();
                collision->Coordinate = coord;
                const uint32_t side = s.TileQuads + 1;
                collision->Positions.reserve( tile.Heights.size() );
                for( uint32_t z = 0; z <= s.TileQuads; ++z ) for( uint32_t x = 0; x <= s.TileQuads; ++x )
                    collision->Positions.emplace_back( static_cast<float>( ( static_cast<int64_t>( coord.X ) * s.TileQuads + x ) * static_cast<double>( s.SampleSpacing ) ),
                        tile.Heights[static_cast<size_t>( z ) * side + x],
                        static_cast<float>( ( static_cast<int64_t>( coord.Z ) * s.TileQuads + z ) * static_cast<double>( s.SampleSpacing ) ) );
                collision->Indices.reserve( static_cast<size_t>( s.TileQuads ) * s.TileQuads * 6 );
                for( uint32_t z = 0; z < s.TileQuads; ++z ) for( uint32_t x = 0; x < s.TileQuads; ++x )
                {
                    const uint32_t a = z * side + x, b = a + side;
                    collision->Indices.insert( collision->Indices.end(), { a, b, a + 1, a + 1, b, b + 1 } );
                }
                data->CollisionTiles.push_back( std::move( collision ) ); ++tileIndex; ++data->BuiltCollisionTiles;
            }
            if( compatible && InPrevious->PatchNeighbors.size() == data->Patches.size() ) data->PatchNeighbors = InPrevious->PatchNeighbors;
            else
            {
                std::map<std::pair<int64_t, int64_t>, int32_t> addresses;
                for( size_t i = 0; i < data->Patches.size(); ++i )
                {
                    const auto sample = data->Patches[i]->Mesh.SampleOrigin;
                    addresses[{ sample.X, sample.Z }] = static_cast<int32_t>( i );
                }
                const int64_t q = s.PatchQuads;
                const SampleCoord directions[] = { { -q, 0 }, { q, 0 }, { 0, -q }, { 0, q } };
                for( const auto& patch : data->Patches )
                {
                    std::array<int32_t, 4> neighbors{ -1, -1, -1, -1 };
                    for( size_t edge = 0; edge < 4; ++edge )
                    {
                        const auto origin = patch->Mesh.SampleOrigin;
                        const auto found = addresses.find( { origin.X + directions[edge].X, origin.Z + directions[edge].Z } );
                        if( found != addresses.end() ) neighbors[edge] = found->second;
                    }
                    data->PatchNeighbors.push_back( neighbors );
                }
            }
            checkCancelled();
            OutData = std::move( data );
            return true;
        }
        catch( const std::exception& e ) { OutError = e.what(); return false; }
    }
}
