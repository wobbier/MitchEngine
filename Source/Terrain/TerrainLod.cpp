#include "PCH.h"
#include "TerrainLod.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace TerrainData
{
    double VerticalPixelScale( const AABB& bounds, const LodView& view )
    {
        const auto infinite = std::numeric_limits<double>::infinity();
        if( view.ForceFine || !std::isfinite( view.PixelsX ) || !std::isfinite( view.PixelsY ) || view.PixelsX <= 0 || view.PixelsY <= 0 || !std::isfinite( view.Near ) || view.Near <= 0 ) return infinite;
        const glm::dvec3 position( view.Position.InternalVector ), front( view.Forward.InternalVector ), up( view.Up.InternalVector );
        const glm::dvec3 min( bounds.Min.InternalVector ), max( bounds.Max.InternalVector );
        for( int i = 0; i < 3; ++i )
            if( !std::isfinite( position[i] ) || !std::isfinite( front[i] ) || !std::isfinite( up[i] ) || !std::isfinite( min[i] ) || !std::isfinite( max[i] ) || min[i] > max[i] ) return infinite;
        if( glm::length( front ) < 1e-8 ) return infinite;
        const auto f = glm::normalize( front ); const auto cross = glm::cross( up, f );
        if( glm::length( cross ) < 1e-8 ) return infinite;
        const auto r = glm::normalize( cross ), u = glm::cross( f, r );
        const auto center = ( min + max ) * 0.5 - position, extents = ( max - min ) * 0.5;
        const double depth = glm::dot( center, f ) - glm::dot( extents, glm::abs( f ) );
        if( depth <= view.Near ) return infinite;
        if( view.Orthographic ) return std::hypot( view.PixelsX * std::abs( r.y ), view.PixelsY * std::abs( u.y ) );
        const double x = std::abs( glm::dot( center, r ) ) + glm::dot( extents, glm::abs( r ) );
        const double y = std::abs( glm::dot( center, u ) ) + glm::dot( extents, glm::abs( u ) );
        return std::hypot( view.PixelsX * ( std::abs( r.y ) / depth + x * std::abs( f.y ) / ( depth * depth ) ),
            view.PixelsY * ( std::abs( u.y ) / depth + y * std::abs( f.y ) / ( depth * depth ) ) );
    }


    bool SelectTerrainLods( const ResidentTerrain& terrain, const Vector3& translation, std::span<const LodView> views,
        const LodSettings& settings, std::span<const PatchLodChoice> previous, std::vector<PatchLodChoice>& out, std::string& error )
    {
        if( !std::isfinite( settings.PixelError ) || settings.PixelError <= 0 || !std::isfinite( settings.Hysteresis ) || settings.Hysteresis < 0 || settings.Hysteresis >= 1 || settings.ForcedLevel < -1 ||
            terrain.PatchNeighbors.size() != terrain.Patches.size() )
        { error = "Invalid terrain LOD settings or neighbor table"; return false; }
        const size_t count = terrain.Patches.size();
        const bool linked = !terrain.NeighborOffsets.empty();
        if( linked )
        {
            if( terrain.NeighborOffsets.size() != count + 1 || terrain.NeighborOffsets.front() != 0 || terrain.NeighborOffsets.back() != terrain.NeighborLinks.size() ) { error = "Invalid hierarchy neighbor ranges"; return false; }
            for( size_t i = 0; i < count; ++i ) if( terrain.NeighborOffsets[i] > terrain.NeighborOffsets[i + 1] ) { error = "Invalid hierarchy neighbor order"; return false; }
            for( const auto& link : terrain.NeighborLinks ) if( link.Index < 0 || link.Index >= int64_t( count ) || link.Edge > 3 ) { error = "Invalid hierarchy neighbor link"; return false; }
        }
        const auto each = [&]( size_t i, const auto& visit ) {
            if( linked ) for( auto n = terrain.NeighborOffsets[i]; n < terrain.NeighborOffsets[i + 1]; ++n ) visit( terrain.NeighborLinks[n].Index, terrain.NeighborLinks[n].Edge );
            else for( uint8_t edge = 0; edge < 4; ++edge ) if( terrain.PatchNeighbors[i][edge] >= 0 ) visit( terrain.PatchNeighbors[i][edge], edge );
        };
        std::vector<PatchLodChoice> choices( count );
        std::vector<std::array<double, 32>> errors( count );
        std::vector<double> scales( count );
        std::vector<uint8_t> minimum( count );
        // An unloaded representation cannot be refined. Propagate its minimum stride before
        // choosing levels, so fine neighbors coarsen into a watertight 2:1 transition band.
        std::deque<size_t> floors;
        for( size_t i = 0; i < count; ++i )
        {
            const auto& patch = terrain.Patches[i];
            if( !patch || patch->BaseLod > 30 || patch->Mesh.Lods.empty() || patch->BaseLod + patch->Mesh.Lods.size() > 32 )
            { error = "Invalid terrain patch LOD data"; return false; }
            minimum[i] = patch->BaseLod; floors.push_back( i );
            for( int32_t n : terrain.PatchNeighbors[i] ) if( n < -1 || n >= static_cast<int64_t>( count ) )
            { error = "Invalid terrain patch neighbor"; return false; }
        }
        while( !floors.empty() )
        {
            const auto i = floors.front(); floors.pop_front();
            each( i, [&]( int32_t n, uint8_t ) { if( minimum[i] > minimum[n] + 1 ) { minimum[n] = minimum[i] - 1; floors.push_back( n ); } } );
        }
        std::deque<size_t> queue; std::vector<bool> queued( count, true );
        for( size_t i = 0; i < count; ++i )
        {
            const auto& patch = terrain.Patches[i];
            if( !patch || patch->Mesh.Lods.empty() || patch->Mesh.Lods.size() > 32 ) { error = "Invalid terrain patch LOD data"; return false; }
            AABB bounds = patch->Mesh.Bounds; bounds.Min += patch->Offset + translation; bounds.Max += patch->Offset + translation;
            scales[i] = views.empty() ? std::numeric_limits<double>::infinity() : 0;
            for( const auto& view : views ) scales[i] = std::max( scales[i], VerticalPixelScale( bounds, view ) );
            double envelope = 0;
            for( size_t level = 0; level < patch->Mesh.Lods.size(); ++level )
            {
                const auto& lod = patch->Mesh.Lods[level];
                if( !std::isfinite( lod.MaxError ) || lod.MaxError < 0 ) { error = "Invalid terrain geometric error"; return false; }
                envelope = std::max( envelope, static_cast<double>( lod.MaxError ) ); errors[i][patch->BaseLod + level] = envelope;
            }
            const int last = patch->BaseLod + static_cast<int>( patch->Mesh.Lods.size() ) - 1;
            if( minimum[i] > last ) { error = "Incompatible terrain representation levels"; return false; }
            int level = minimum[i];
            if( settings.ForcedLevel >= 0 ) level = std::clamp( settings.ForcedLevel, int( minimum[i] ), last );
            else if( std::isfinite( scales[i] ) )
            {
                const int old = previous.size() == count && previous[i].Level <= last ? previous[i].Level : -1;
                for( int candidate = last; candidate >= minimum[i]; --candidate )
                {
                    const double threshold = old >= 0 && candidate > old ? settings.PixelError * ( 1.0 - settings.Hysteresis ) : settings.PixelError;
                    if( errors[i][candidate] * scales[i] <= threshold ) { level = candidate; break; }
                }
            }
            choices[i].Level = static_cast<uint8_t>( level ); queue.push_back( i );
            for( int32_t neighbor : terrain.PatchNeighbors[i] )
                if( neighbor < -1 || neighbor >= static_cast<int64_t>( count ) ) { error = "Invalid terrain patch neighbor"; return false; }
        }
        const auto enqueue = [&]( size_t i ) {
            if( !queued[i] ) { queued[i] = true; queue.push_back( i ); }
            each( i, [&]( int32_t n, uint8_t ) { if( !queued[n] ) { queued[n] = true; queue.push_back( n ); } } );
        };
        while( !queue.empty() )
        {
            const size_t i = queue.front(); queue.pop_front(); queued[i] = false;
            each( i, [&]( int32_t n, uint8_t ) {
                // A one-quad fine patch cannot contract an edge again. Its coarser neighbor
                // must retain a vertex at that patch boundary, even across a larger footprint.
                const auto last = terrain.Patches[n]->BaseLod + terrain.Patches[n]->Mesh.Lods.size() - 1;
                const auto cap = std::min<size_t>( choices[n].Level + 1, last );
                if( choices[i].Level > cap ) { choices[i].Level = uint8_t( cap ); enqueue( i ); }
            } );
            const size_t level = choices[i].Level;
            // Each stitched triangle remains inside one next-level triangle. Relative to that
            // common plane, both surfaces deviate by at most E(next), hence a bound of 2E(next).
            if( settings.ForcedLevel < 0 && level + 1 < terrain.Patches[i]->BaseLod + terrain.Patches[i]->Mesh.Lods.size() &&
                ( !std::isfinite( scales[i] ) || 2 * errors[i][level + 1] * scales[i] > settings.PixelError ) )
                each( i, [&]( int32_t n, uint8_t ) { if( choices[n].Level > level && minimum[n] <= level ) { choices[n].Level = static_cast<uint8_t>( level ); enqueue( n ); } } );
        }
        for( size_t i = 0; i < count; ++i )
        {
            each( i, [&]( int32_t n, uint8_t edge ) { if( choices[n].Level > choices[i].Level ) choices[i].Edges |= 1 << edge; } );
            const auto level = choices[i].Level;
            const double bound = choices[i].Edges ? 2 * errors[i][level + 1] : errors[i][level];
            choices[i].PixelError = bound == 0 ? 0 : bound * scales[i];
        }
        out = std::move( choices ); error.clear(); return true;
    }
}
