#include "PCH.h"
#include "TerrainStreamCache.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace TerrainData
{
    namespace
    {
        uint64_t GeometryBytes( const PatchMesh& mesh )
        {
            uint64_t bytes = sizeof( ResidentPatch ) + mesh.Vertices.size() * sizeof( PatchVertex );
            for( const auto& lod : mesh.Lods ) bytes += sizeof( PatchLod ) + lod.Indices.size() * sizeof( uint32_t );
            return bytes;
        }
        uint64_t UploadBytes( const PatchMesh& mesh )
        {
            // Reserve padding beyond the current 56-byte vertex layout, plus base/wire indices.
            return mesh.Vertices.size() * 80 + mesh.Lods.front().Indices.size() * 12;
        }
    }


    StreamLimits TerrainStreamCache::TileReservation( const Settings& s, bool compressed )
    {
        const uint64_t q = s.PatchQuads, count = uint64_t( s.TileQuads / q ) * ( s.TileQuads / q );
        const uint64_t vertices = ( q + 1 ) * ( q + 1 ) * count, indices = q * q * 6 * count;
        StreamLimits cost;
        // Canonical heights/normals during decode, prepared vertices/levels/collision, retained
        // MeshData CPU copies, vector/map bookkeeping, and overlap during upload retirement.
        cost.CpuBytes = s.SamplesPerTile() * 32 + vertices * 80 + indices * 28 + count * 2048 + 4096;
        if( compressed ) cost.CpuBytes += s.SamplesPerTile() * 16 + 8; // stored + decoded buffers overlap
        cost.GpuBytes = 2 * ( vertices * 80 + indices * 12 );
        // Box3D's BVH, adjacency, weld/build scratch and publication overlap. Reserved, not a
        // claim about exact backend allocations; 512 bytes per canonical triangle is conservative.
        cost.CollisionBytes = uint64_t( s.TileQuads ) * s.TileQuads * 2 * 512 + s.SamplesPerTile() * 32 + 4096;
        return cost;
    }


    TerrainStreamCache::TerrainStreamCache( std::shared_ptr<const StreamArchive> archive, Loader loader )
        : m_archive( std::move( archive ) ), m_loader( std::move( loader ) ), m_snapshot( m_archive->Coverage )
    {
        m_tileCost = TileReservation( m_archive->Config, m_archive->Compressed );
        m_pageCost = CoverageReservation( m_archive->Config, m_archive->CoverageLod, bool( m_archive->Coverage->Heights->GetSurface() ), m_archive->Compressed );
        m_baseCost.CpuBytes = sizeof( StreamArchive ) + m_archive->Header.size() + m_archive->Blocks.size() * ( sizeof( StreamBlock ) + 256 );
        m_baseCost.GpuBytes = m_baseCost.CollisionBytes = 0;
        if( m_archive->Coverage->Heights->GetSurface() ) m_baseCost.CpuBytes += 65536; // bounded palette and paths
        for( const auto& patch : m_archive->Coverage->Patches )
        {
            m_baseCost.CpuBytes += GeometryBytes( patch->Mesh ) + patch->Mesh.Vertices.size() * 24 + patch->Mesh.Lods.front().Indices.size() * 16 + 1024;
            m_baseCost.GpuBytes += 2 * UploadBytes( patch->Mesh );
        }
        // Shared stitched index patterns and their lazy wireframes, including full/coarse grids.
        uint64_t patternIndices = 0;
        for( uint32_t q = m_archive->Config.PatchQuads; q; q /= 2 ) patternIndices += uint64_t( q ) * q * 6;
        m_baseCost.GpuBytes += patternIndices * 16 * 12;
        m_baseCost.CpuBytes += patternIndices * 16 * 12;
        Measure();
    }


    TerrainStreamCache::~TerrainStreamCache()
    {
        // The worker captures only immutable archive/loader values. Joining here also prevents
        // an old scene's completion from publishing into a replacement cache or a dead world.
        if( m_job.valid() ) m_job.wait();
    }


    void TerrainStreamCache::WaitForRequest() { if( m_job.valid() ) m_job.wait(); }


    bool TerrainStreamCache::CanRetire()
    {
        if( m_job.valid() ) return false;
        for( const auto& [coord, entry] : m_entries ) if( entry.Tile || entry.Loading ) return false;
        for( const auto& [coord, entry] : m_pages ) if( entry.Page || entry.Loading ) return false;
        m_snapshot = m_archive->Coverage; Measure();
        if( !m_snapshots.empty() ) return false;
        for( const auto& patch : m_archive->Coverage->Patches ) if( patch.use_count() > 1 ) return false;
        return true;
    }


    void TerrainStreamCache::InvalidatePending()
    {
        for( auto& [coord, entry] : m_entries ) if( entry.Loading )
        { entry.Request = ++m_request; entry.Active = false; ++m_stats.Cancelled; }
        for( auto& [coord, entry] : m_pages ) if( entry.Loading )
        { entry.Request = ++m_request; entry.Active = false; ++m_stats.Cancelled; }
        m_publishDirty = true;
    }


    bool TerrainStreamCache::HasConsumers( const Entry& entry ) const
    {
        if( !entry.Tile ) return false;
        if( entry.Tile.use_count() > 1 || entry.Tile->Collision.use_count() > 1 ) return true;
        for( const auto& patch : entry.Tile->Patches ) if( patch.use_count() > 1 ) return true;
        return false;
    }


    void TerrainStreamCache::Measure()
    {
        m_stats.CpuReserved = m_baseCost.CpuBytes * m_limits.Instances; m_stats.GpuReserved = m_baseCost.GpuBytes * m_limits.Instances; m_stats.CollisionReserved = 0;
        m_stats.Resident = m_stats.Detail = m_stats.Retiring = 0;
        m_stats.QueryPins = m_stats.DrawPins = m_stats.CollisionPins = 0;
        m_stats.CoverageResident = m_stats.CoverageRetiring = 0;
        for( const auto& [coord, entry] : m_pages ) if( entry.Page || entry.Loading )
        {
            m_stats.CpuReserved += m_pageCost.CpuBytes * m_limits.Instances;
            m_stats.GpuReserved += m_pageCost.GpuBytes * m_limits.Instances;
            if( entry.Page ) { if( entry.Active ) ++m_stats.CoverageResident; else ++m_stats.CoverageRetiring; }
        }
        for( const auto& [coord, entry] : m_entries )
        {
            if( entry.Tile || entry.Loading ) m_stats.CpuReserved += m_tileCost.CpuBytes * m_limits.Instances;
            if( entry.Visual ) m_stats.GpuReserved += m_tileCost.GpuBytes * m_limits.Instances;
            if( entry.Active && entry.UseDetail ) ++m_stats.Detail;
            if( entry.Collision ) m_stats.CollisionReserved += m_tileCost.CollisionBytes * m_limits.Instances;
            if( entry.Tile ) { if( entry.Active ) ++m_stats.Resident; else ++m_stats.Retiring; }
            if( entry.Tile && !entry.Active )
            {
                m_stats.QueryPins += entry.Tile.use_count() - 1;
                m_stats.CollisionPins += entry.Tile->Collision.use_count() - 1;
                for( const auto& patch : entry.Tile->Patches ) m_stats.DrawPins += patch.use_count() - 1;
            }
        }
        for( auto it = m_snapshots.begin(); it != m_snapshots.end(); )
        {
            if( auto snapshot = it->lock() )
            {
                m_stats.CpuReserved += sizeof( ResidentTerrain ) + snapshot->Patches.size() * ( sizeof( void* ) + sizeof( std::array<int32_t, 4> ) );
                m_stats.CpuReserved += snapshot->CoveragePatches.size() * sizeof( void* ) + snapshot->CoverageTiles.size() * sizeof( TileCoord );
                m_stats.CpuReserved += snapshot->Heights->GetTiles().size() * ( m_archive->Config.SamplesPerTile() * 4 + 256 ); ++it;
            }
            else it = m_snapshots.erase( it );
        }
        m_stats.Loading = m_job.valid();
    }


    bool TerrainStreamCache::Publish( const std::map<TileCoord, TileDemand>& demand )
    {
        const auto& s = m_archive->Config;
        uint64_t bytes = sizeof( ResidentTerrain ) + m_snapshot->Patches.size() * ( sizeof( void* ) + sizeof( std::array<int32_t, 4> ) );
        for( const auto& [coord, entry] : m_entries ) if( entry.Active && entry.Tile ) bytes += s.SamplesPerTile() * 4 + 256;
        Measure();
        if( bytes > m_limits.CpuBytes || m_stats.CpuReserved > m_limits.CpuBytes - bytes )
        {
            m_stats.Pressure = "CPU budget is pinned by current or retiring terrain snapshots";
            // The immutable coverage snapshot already exists. It can retire old query/collider
            // pins without allocating another heightfield while publication has no headroom.
            if( !m_snapshot->ResidencyPins.empty() ) { m_snapshot = m_archive->Coverage; m_publishedDemand.clear(); }
            return false;
        }
        auto data = std::make_shared<ResidentTerrain>( *m_archive->Coverage ); auto heights = std::make_shared<Heightfield>( s );
        const size_t patchesPerTile = size_t( s.TileQuads / s.PatchQuads ) * ( s.TileQuads / s.PatchQuads );
        std::string error;
        for( const auto& [coord, entry] : m_entries ) if( entry.Active && entry.Tile )
        {
            if( !heights->LoadTile( coord, entry.Tile->Heights, error ) )
            { m_stats.Error = "Terrain stream borders failed validation: " + error; return false; }
            data->ResidencyPins.push_back( entry.Tile );
            if( entry.UseDetail )
                std::copy( entry.Tile->Patches.begin(), entry.Tile->Patches.end(), data->Patches.begin() + m_archive->TileIndex( coord ) * patchesPerTile );
            if( entry.Collision ) if( auto wanted = demand.find( coord ); wanted != demand.end() && wanted->second.Collision ) data->CollisionTiles.push_back( entry.Tile->Collision );
        }
        for( const auto& [coord, request] : demand ) if( !heights->FindTile( coord ) )
        {
            const auto entry = m_entries.find( coord );
            if( entry == m_entries.end() || !entry->second.Failed ) heights->SetPending( coord, true );
        }
        heights->ClearDirty(); data->Heights = heights;
        m_snapshot = data; m_snapshots.push_back( data ); m_publishedDemand = demand; return true;
    }


    void TerrainStreamCache::Tick( uint64_t frame, std::span<const TileDemand> requests, const StreamLimits& limits, bool allowLoad )
    {
        if( m_archive->PagedCoverage ) { TickPaged( frame, requests, limits, allowLoad ); return; }
        m_limits = limits; m_stats.Pressure.clear();
        if( !limits.Instances || limits.Instances > 65536 )
        { m_stats.CoverageReady = m_stats.CollisionReady = false; m_stats.Pressure = "Invalid terrain streaming limits"; return; }
        m_stats.CoverageReady = m_baseCost.CpuBytes * limits.Instances <= limits.CpuBytes && m_baseCost.GpuBytes * limits.Instances <= limits.GpuBytes;
        std::map<TileCoord, TileDemand> demand;
        for( const auto& request : requests ) if( m_archive->Config.Contains( request.Coordinate ) && std::isfinite( request.Priority ) )
        {
            auto [it, inserted] = demand.emplace( request.Coordinate, request );
            if( !inserted ) { it->second.Collision |= request.Collision; it->second.Visual |= request.Visual; it->second.Prefetch |= request.Prefetch; it->second.Priority = std::min( it->second.Priority, request.Priority ); }
        }
        // Admit the closest optional demand that can fit, rather than retaining every tile
        // inside a wide camera radius and preventing higher-priority new detail from entering.
        std::vector<TileDemand> ranked; for( const auto& [coord, request] : demand ) ranked.push_back( request );
        std::stable_sort( ranked.begin(), ranked.end(), []( const auto& a, const auto& b ) {
            if( a.Collision != b.Collision ) return a.Collision;
            if( a.Prefetch != b.Prefetch ) return a.Prefetch;
            if( a.Priority != b.Priority ) return a.Priority < b.Priority;
            return a.Coordinate < b.Coordinate;
        } );
        const uint64_t baseBytes = m_baseCost.CpuBytes * limits.Instances + m_snapshot->Patches.size() * 128 + 4096;
        const uint64_t perTile = m_tileCost.CpuBytes * limits.Instances + m_archive->Config.SamplesPerTile() * 16 + 1024;
        const uint64_t slots = limits.CpuBytes > baseBytes ? ( limits.CpuBytes - baseBytes ) / perTile : 0;
        size_t admitted = 0;
        for( const auto& request : ranked )
        {
            if( request.Collision || admitted < slots ) ++admitted;
            else demand.erase( request.Coordinate );
        }
        if( demand.size() < ranked.size() ) m_stats.Pressure = "CPU budget limits optional camera detail to the closest tiles";
        const uint64_t baseGpu = m_baseCost.GpuBytes * limits.Instances;
        const uint64_t gpuSlots = limits.GpuBytes > baseGpu ? ( limits.GpuBytes - baseGpu ) / ( m_tileCost.GpuBytes * limits.Instances ) : 0;
        uint64_t visualCount = 0;
        for( const auto& request : ranked ) if( auto selected = demand.find( request.Coordinate ); selected != demand.end() && selected->second.Visual )
        {
            if( visualCount++ >= gpuSlots ) { selected->second.Visual = false; m_stats.Pressure = "GPU budget limits uploaded detail to the closest tiles"; }
        }
        bool changed = demand.size() != m_publishedDemand.size();
        if( !changed ) for( const auto& [coord, request] : demand )
        {
            const auto old = m_publishedDemand.find( coord );
            if( old == m_publishedDemand.end() || old->second.Collision != request.Collision || old->second.Visual != request.Visual || old->second.Prefetch != request.Prefetch ) { changed = true; break; }
        }
        for( auto& [coord, entry] : m_entries )
        {
            const auto wanted = demand.find( coord );
            if( wanted != demand.end() )
            {
                entry.LastDemand = frame; if( wanted->second.Visual ) entry.LastVisual = frame;
                if( !entry.Active && !entry.Failed ) { entry.Active = true; changed = true; }
            }
            else if( entry.Active && ( entry.Loading || frame - std::min( frame, entry.LastDemand ) >= limits.HoldFrames ) )
            { entry.Active = false; changed = true; if( entry.Loading ) { entry.Request = ++m_request; ++m_stats.Cancelled; } }
        }
        if( m_job.valid() && m_job.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
        {
            Completion result = m_job.get(); auto found = m_entries.find( result.Coordinate );
            if( found != m_entries.end() )
            {
                auto& entry = found->second; entry.Loading = false;
                if( entry.Request == result.Request && entry.Active )
                {
                    if( result.Tile ) { entry.Tile = std::move( result.Tile ); ++m_stats.Loaded; m_stats.BytesRead += m_archive->Blocks[m_archive->TileIndex( result.Coordinate )].Size; m_stats.DecodedBytesRead += m_archive->Blocks[m_archive->TileIndex( result.Coordinate )].DecodedSize; }
                    else { entry.Failed = true; entry.Active = false; ++m_stats.Failed; m_stats.Error = result.Error; }
                }
                else entry.Active = false;
                changed = true;
            }
        }
        // Publish removal first, so render and physics can release their own pins without a
        // new allocation waiting forever for the old region to fit beside a teleported region.
        for( auto it = m_entries.begin(); it != m_entries.end(); )
        {
            auto& entry = it->second;
            if( !entry.Active && !entry.Loading && !entry.Failed && !HasConsumers( entry ) )
            { if( entry.Tile ) ++m_stats.Evicted; it = m_entries.erase( it ); continue; }
            if( !entry.Active && !HasConsumers( entry ) ) entry.Visual = entry.Collision = false;
            if( entry.Visual && !entry.UseDetail && entry.Tile && std::all_of( entry.Tile->Patches.begin(), entry.Tile->Patches.end(), []( const auto& patch ) { return patch.use_count() == 1; } ) ) entry.Visual = false;
            if( entry.Collision && entry.Tile && entry.Tile->Collision.use_count() == 1 )
            { const auto wanted = demand.find( it->first ); if( wanted == demand.end() || !wanted->second.Collision ) entry.Collision = false; }
            ++it;
        }
        Measure();
        // Drop optional, no-longer-demanded tiles immediately under pressure; ordinary travel
        // retains them for HoldFrames. Reservations remain charged while consumers retire them.
        const bool cpuPressure = m_stats.CpuReserved + m_tileCost.CpuBytes * limits.Instances > limits.CpuBytes;
        if( cpuPressure ) for( auto& [coord, entry] : m_entries ) if( entry.Active && !demand.contains( coord ) ) { entry.Active = false; changed = true; }
        const bool gpuPressure = m_stats.GpuReserved + m_tileCost.GpuBytes * limits.Instances > limits.GpuBytes;
        for( auto& [coord, entry] : m_entries ) if( entry.UseDetail )
        {
            const auto wanted = demand.find( coord );
            if( ( wanted == demand.end() || !wanted->second.Visual ) && ( gpuPressure || frame - std::min( frame, entry.LastVisual ) >= limits.HoldFrames ) )
            { entry.UseDetail = false; changed = true; }
        }
        std::vector<TileDemand> ordered; for( const auto& [coord, request] : demand ) ordered.push_back( request );
        std::stable_sort( ordered.begin(), ordered.end(), []( const auto& a, const auto& b ) {
            if( a.Collision != b.Collision ) return a.Collision;
            if( a.Prefetch != b.Prefetch ) return a.Prefetch;
            if( a.Priority != b.Priority ) return a.Priority < b.Priority;
            return a.Coordinate < b.Coordinate;
        } );
        m_stats.Pending = 0;
        for( const auto& request : ordered )
        {
            auto found = m_entries.find( request.Coordinate );
            if( found != m_entries.end() && found->second.Failed ) continue;
            if( found == m_entries.end() )
            {
                ++m_stats.Pending;
                if( m_job.valid() || !allowLoad ) continue;
                const uint64_t snapshotAllowance = m_snapshot->Patches.size() * 32 + ( m_stats.Resident + 1 ) * ( m_archive->Config.SamplesPerTile() * 4 + 256 ) + 4096;
                if( m_stats.CpuReserved > limits.CpuBytes || m_tileCost.CpuBytes * limits.Instances + snapshotAllowance > limits.CpuBytes - m_stats.CpuReserved )
                { m_stats.Pressure = "CPU budget cannot admit the requested detail; coarse coverage remains visible"; continue; }
                auto& entry = m_entries[request.Coordinate]; entry.Active = entry.Loading = true; entry.LastDemand = frame; entry.Request = ++m_request;
                const auto archive = m_archive; const auto loader = m_loader; const uint64_t generation = entry.Request; const auto coord = request.Coordinate;
                try
                {
                    m_job = std::async( std::launch::async, [archive, loader, coord, generation]() {
                        Completion result; result.Coordinate = coord; result.Request = generation;
                        try { if( !loader( *archive, coord, result.Tile, result.Error ) ) result.Tile.reset(); }
                        catch( const std::exception& e ) { result.Error = e.what(); }
                        return result;
                    } );
                }
                catch( const std::exception& e )
                { entry.Loading = entry.Active = false; entry.Failed = true; ++m_stats.Failed; m_stats.Error = e.what(); }
                changed = true; Measure(); continue;
            }
            auto& entry = found->second;
            if( !entry.Tile ) { ++m_stats.Pending; continue; }
            if( request.Visual && !entry.Visual )
            {
                if( m_stats.GpuReserved <= limits.GpuBytes && m_tileCost.GpuBytes * limits.Instances <= limits.GpuBytes - m_stats.GpuReserved ) { entry.Visual = true; Measure(); }
                else m_stats.Pressure = "GPU budget is full; retaining stitched coarse coverage";
            }
            if( request.Visual && entry.Visual && !entry.UseDetail ) { entry.UseDetail = true; entry.LastVisual = frame; changed = true; }
            if( request.Collision && !entry.Collision )
            {
                if( m_stats.CollisionReserved <= limits.CollisionBytes && m_tileCost.CollisionBytes * limits.Instances <= limits.CollisionBytes - m_stats.CollisionReserved ) { entry.Collision = true; changed = true; Measure(); }
                else m_stats.Pressure = "Collision budget is pinned; physics waits for the required region";
            }
            if( !request.Collision && entry.Collision && entry.Tile->Collision.use_count() == 1 ) { entry.Collision = false; changed = true; }
        }
        m_publishDirty |= changed;
        if( m_publishDirty ) m_publishDirty = !Publish( demand );
        m_stats.CollisionReady = true;
        for( const auto& request : ordered ) if( request.Collision )
        {
            const auto found = m_entries.find( request.Coordinate );
            if( found == m_entries.end() || !found->second.Tile || !found->second.Active || !found->second.Collision || found->second.Failed ||
                std::find( m_snapshot->CollisionTiles.begin(), m_snapshot->CollisionTiles.end(), found->second.Tile->Collision ) == m_snapshot->CollisionTiles.end() ) m_stats.CollisionReady = false;
        }
        CountPrefetch( demand );
        Measure();
        if( m_baseCost.CpuBytes * limits.Instances > limits.CpuBytes || m_baseCost.GpuBytes * limits.Instances > limits.GpuBytes )
        { m_stats.Pressure = "Coarse terrain coverage exceeds the configured CPU/GPU budget; increase budgets or recook a coarser/smaller dataset"; m_stats.CollisionReady = false; }
    }


    void TerrainStreamCache::CountPrefetch( const std::map<TileCoord, TileDemand>& demand )
    {
        m_stats.PrefetchResident = m_stats.PrefetchPending = 0;
        for( const auto& [coord, request] : demand ) if( request.Prefetch && !request.Collision )
        {
            const auto entry = m_entries.find( coord );
            if( entry != m_entries.end() && entry->second.Active && entry->second.Tile ) ++m_stats.PrefetchResident;
            else if( entry == m_entries.end() || !entry->second.Failed ) ++m_stats.PrefetchPending;
        }
    }


    void AddStreamDemand( const Settings& s, const Vector3& position, double radius, bool collision, std::map<TileCoord, TileDemand>& demand, bool coverage, bool prefetch )
    {
        if( !std::isfinite( position.x ) || !std::isfinite( position.z ) || !std::isfinite( radius ) || radius < 0 ) return;
        const double width = double( s.TileQuads ) * s.SampleSpacing;
        const double minX = std::max( double( s.MinTile.X ), std::floor( ( position.x - radius ) / width ) );
        const double minZ = std::max( double( s.MinTile.Z ), std::floor( ( position.z - radius ) / width ) );
        const double maxX = std::min( double( s.MinTile.X ) + s.TilesX - 1, std::floor( ( position.x + radius ) / width ) );
        const double maxZ = std::min( double( s.MinTile.Z ) + s.TilesZ - 1, std::floor( ( position.z + radius ) / width ) );
        if( minX > maxX || minZ > maxZ ) return;
        for( int64_t x = static_cast<int64_t>( minX ); x <= static_cast<int64_t>( maxX ); ++x ) for( int64_t z = static_cast<int64_t>( minZ ); z <= static_cast<int64_t>( maxZ ); ++z )
        {
            const TileCoord coord{ static_cast<int32_t>( x ), static_cast<int32_t>( z ) };
            const double dx = ( x + 0.5 ) * width - position.x, dz = ( z + 0.5 ) * width - position.z;
            const double priority = dx * dx + dz * dz;
            auto [it, added] = demand.emplace( coord, TileDemand{ coord, collision, !collision && !coverage && !prefetch, priority, coverage, prefetch } );
            if( !added ) { it->second.Collision |= collision; it->second.Visual |= !collision && !coverage && !prefetch; it->second.Coverage |= coverage; it->second.Prefetch |= prefetch; it->second.Priority = std::min( it->second.Priority, priority ); }
        }
    }
}
