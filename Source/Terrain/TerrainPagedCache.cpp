#include "PCH.h"
#include "TerrainStreamCache.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace TerrainData
{
    StreamLimits TerrainStreamCache::CoverageReservation( const Settings& s, uint8_t base )
    {
        const uint64_t q = s.PatchQuads >> base, patches = uint64_t( s.TileQuads / s.PatchQuads ) * ( s.TileQuads / s.PatchQuads );
        const uint64_t vertices = ( q + 1 ) * ( q + 1 ) * patches, indices = q * q * 6 * patches;
        StreamLimits cost; cost.CpuBytes = vertices * 128 + indices * 28 + patches * 2048 + 2048;
        cost.GpuBytes = 2 * ( vertices * 80 + indices * 12 ); cost.CollisionBytes = 0; return cost;
    }


    bool TerrainStreamCache::HasConsumers( const CoverageEntry& entry ) const
    {
        if( !entry.Page ) return false;
        if( entry.Page.use_count() > 1 ) return true;
        for( const auto& patch : entry.Page->Patches ) if( patch.use_count() > 1 ) return true;
        return false;
    }


    bool TerrainStreamCache::PublishPaged( const std::map<TileCoord, TileDemand>& demand )
    {
        const auto& s = m_archive->Config;
        std::vector<TileCoord> window;
        bool complete = true;
        for( const auto& [coord, request] : demand ) if( request.Coverage )
        {
            const auto found = m_pages.find( coord );
            if( found == m_pages.end() || !found->second.Active || !found->second.Page || found->second.Failed ) { complete = false; break; }
        }
        for( const auto& [coord, request] : demand ) if( request.Coverage )
        {
            const auto found = m_pages.find( coord );
            if( found != m_pages.end() && found->second.Active && found->second.Page &&
                ( complete || std::binary_search( m_snapshot->CoverageTiles.begin(), m_snapshot->CoverageTiles.end(), coord ) ) ) window.push_back( coord );
        }
        const size_t perTile = size_t( s.TileQuads / s.PatchQuads ) * ( s.TileQuads / s.PatchQuads );
        uint64_t bytes = sizeof( ResidentTerrain ) + window.size() * ( perTile * ( sizeof( void* ) * 2 + sizeof( std::array<int32_t, 4> ) ) + sizeof( TileCoord ) );
        for( const auto& [coord, entry] : m_entries ) if( entry.Active && entry.Tile ) bytes += s.SamplesPerTile() * 4 + 256;
        Measure();
        if( bytes > m_limits.CpuBytes || m_stats.CpuReserved > m_limits.CpuBytes - bytes )
        {
            m_stats.Pressure = "Paged terrain publication waits for pinned CPU reservations to retire";
            m_snapshot = m_archive->Coverage; m_publishedDemand.clear(); return false;
        }
        auto data = std::make_shared<ResidentTerrain>( *m_archive->Coverage ); auto heights = std::make_shared<Heightfield>( s );
        std::string error;
        for( const auto& coord : window )
        {
            const auto& page = m_pages.at( coord ).Page; data->CoverageTiles.push_back( coord ); data->ResidencyPins.push_back( page );
            data->CoveragePatches.insert( data->CoveragePatches.end(), page->Patches.begin(), page->Patches.end() );
            const auto detail = m_entries.find( coord );
            const auto& patches = detail != m_entries.end() && detail->second.Active && detail->second.Tile && detail->second.UseDetail ? detail->second.Tile->Patches : page->Patches;
            data->Patches.insert( data->Patches.end(), patches.begin(), patches.end() );
        }
        for( const auto& [coord, entry] : m_entries ) if( entry.Active && entry.Tile )
        {
            if( !heights->LoadTile( coord, entry.Tile->Heights, error ) ) { m_stats.Error = "Paged terrain borders failed validation: " + error; return false; }
            data->ResidencyPins.push_back( entry.Tile );
            const auto wanted = demand.find( coord );
            if( entry.Collision && wanted != demand.end() && wanted->second.Collision ) data->CollisionTiles.push_back( entry.Tile->Collision );
        }
        for( const auto& [coord, request] : demand ) if( ( request.Collision || request.Visual ) && !heights->FindTile( coord ) )
        {
            const auto entry = m_entries.find( coord ); if( entry == m_entries.end() || !entry->second.Failed ) heights->SetPending( coord, true );
        }
        heights->ClearDirty(); data->Heights = heights; BuildPatchNeighbors( *data );
        m_snapshot = data; m_snapshots.push_back( data ); m_publishedDemand = demand; return true;
    }


    void TerrainStreamCache::TickPaged( uint64_t frame, std::span<const TileDemand> requests, const StreamLimits& limits, bool allowLoad )
    {
        m_limits = limits; m_stats.Pressure.clear();
        if( !limits.Instances || limits.Instances > 65536 ) { m_stats.CoverageReady = m_stats.CollisionReady = false; m_stats.Pressure = "Invalid paged terrain limits"; return; }
        m_stats.CoverageReady = m_baseCost.CpuBytes * limits.Instances <= limits.CpuBytes && m_baseCost.GpuBytes * limits.Instances <= limits.GpuBytes;
        std::map<TileCoord, TileDemand> demand;
        for( auto request : requests ) if( m_archive->Config.Contains( request.Coordinate ) && std::isfinite( request.Priority ) )
        {
            request.Coverage |= request.Visual;
            auto [it, added] = demand.emplace( request.Coordinate, request );
            if( !added ) { it->second.Collision |= request.Collision; it->second.Visual |= request.Visual; it->second.Coverage |= request.Coverage; it->second.Priority = std::min( it->second.Priority, request.Priority ); }
        }
        bool changed = demand.size() != m_publishedDemand.size();
        for( const auto& [coord, request] : demand )
        {
            const auto old = m_publishedDemand.find( coord );
            if( old == m_publishedDemand.end() || old->second.Collision != request.Collision || old->second.Visual != request.Visual || old->second.Coverage != request.Coverage ) changed = true;
        }
        for( auto& [coord, entry] : m_entries )
        {
            const auto wanted = demand.find( coord ); const bool active = wanted != demand.end() && ( wanted->second.Collision || wanted->second.Visual );
            if( active ) { entry.LastDemand = frame; if( !entry.Active && !entry.Failed ) { entry.Active = true; changed = true; } }
            else if( entry.Active && ( entry.Loading || frame - std::min( frame, entry.LastDemand ) >= limits.HoldFrames ) )
            { entry.Active = false; changed = true; if( entry.Loading ) { entry.Request = ++m_request; ++m_stats.Cancelled; } }
            if( entry.UseDetail && ( !active || !wanted->second.Visual ) ) { entry.UseDetail = false; changed = true; }
        }
        for( auto& [coord, entry] : m_pages )
        {
            const auto wanted = demand.find( coord ); const bool active = wanted != demand.end() && wanted->second.Coverage;
            if( active && !entry.Failed ) { if( !entry.Active ) changed = true; entry.Active = true; entry.LastDemand = frame; }
            else if( entry.Active ) { entry.Active = false; changed = true; if( entry.Loading ) { entry.Request = ++m_request; ++m_stats.Cancelled; } }
        }
        if( m_job.valid() && m_job.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
        {
            Completion result = m_job.get(); changed = true;
            if( result.Coverage )
            {
                auto& entry = m_pages.at( result.Coordinate ); entry.Loading = false;
                if( entry.Active && entry.Request == result.Request )
                {
                    if( result.Page ) { entry.Page = std::move( result.Page ); ++m_stats.CoverageLoaded; m_stats.BytesRead += m_archive->Blocks[m_archive->TileIndex( result.Coordinate )].CoverageSize; }
                    else { entry.Failed = true; entry.Active = false; ++m_stats.Failed; m_stats.Error = result.Error; }
                }
                else entry.Active = false;
            }
            else
            {
                auto& entry = m_entries.at( result.Coordinate ); entry.Loading = false;
                if( entry.Active && entry.Request == result.Request )
                {
                    if( result.Tile ) { entry.Tile = std::move( result.Tile ); ++m_stats.Loaded; m_stats.BytesRead += m_archive->Blocks[m_archive->TileIndex( result.Coordinate )].Size; }
                    else { entry.Failed = true; entry.Active = false; ++m_stats.Failed; m_stats.Error = result.Error; }
                }
                else entry.Active = false;
            }
        }
        for( auto it = m_entries.begin(); it != m_entries.end(); )
        {
            auto& entry = it->second;
            if( !entry.Active && !entry.Loading && !entry.Failed && !HasConsumers( entry ) ) { if( entry.Tile ) ++m_stats.Evicted; it = m_entries.erase( it ); changed = true; continue; }
            if( entry.Visual && !entry.UseDetail && entry.Tile && std::all_of( entry.Tile->Patches.begin(), entry.Tile->Patches.end(), []( const auto& p ) { return p.use_count() == 1; } ) ) entry.Visual = false;
            if( entry.Collision && entry.Tile && entry.Tile->Collision.use_count() == 1 ) { const auto wanted = demand.find( it->first ); if( wanted == demand.end() || !wanted->second.Collision ) entry.Collision = false; }
            ++it;
        }
        for( auto it = m_pages.begin(); it != m_pages.end(); )
            if( !it->second.Active && !it->second.Loading && !it->second.Failed && !HasConsumers( it->second ) ) { if( it->second.Page ) ++m_stats.CoverageEvicted; it = m_pages.erase( it ); changed = true; } else ++it;
        Measure();
        const auto fits = [&]( uint64_t cpu, uint64_t gpu ) {
            return m_stats.CpuReserved <= limits.CpuBytes && cpu <= limits.CpuBytes - m_stats.CpuReserved && m_stats.GpuReserved <= limits.GpuBytes && gpu <= limits.GpuBytes - m_stats.GpuReserved;
        };
        struct Task { TileDemand Demand; int Kind; }; std::vector<Task> tasks;
        for( const auto& [coord, request] : demand )
        {
            if( request.Collision ) tasks.push_back( { request, 0 } );
            if( request.Coverage ) tasks.push_back( { request, 1 } );
            if( request.Visual && !request.Collision ) tasks.push_back( { request, 2 } );
        }
        std::stable_sort( tasks.begin(), tasks.end(), []( const auto& a, const auto& b ) { if( a.Kind != b.Kind ) return a.Kind < b.Kind; if( a.Demand.Priority != b.Demand.Priority ) return a.Demand.Priority < b.Demand.Priority; return a.Demand.Coordinate < b.Demand.Coordinate; } );
        m_stats.Pending = m_stats.CoveragePending = 0;
        for( const auto& task : tasks )
        {
            const auto coord = task.Demand.Coordinate;
            if( task.Kind == 1 )
            {
                auto found = m_pages.find( coord ); if( found != m_pages.end() && ( found->second.Page || found->second.Failed ) ) continue;
                ++m_stats.Pending; ++m_stats.CoveragePending;
                if( m_job.valid() || !allowLoad || !m_stats.CoverageReady || !fits( m_pageCost.CpuBytes * limits.Instances, m_pageCost.GpuBytes * limits.Instances ) ) continue;
                auto& entry = m_pages[coord]; entry.Active = entry.Loading = true; entry.Request = ++m_request; entry.LastDemand = frame;
                const auto archive = m_archive; const auto generation = entry.Request;
                try { m_job = std::async( std::launch::async, [archive, coord, generation]() { Completion result; result.Coordinate = coord; result.Request = generation; result.Coverage = true; try { ReadStreamCoverage( *archive, coord, result.Page, result.Error ); } catch( const std::exception& e ) { result.Error = e.what(); } return result; } ); }
                catch( const std::exception& e ) { entry.Active = entry.Loading = false; entry.Failed = true; m_stats.Error = e.what(); ++m_stats.Failed; }
                changed = true; Measure(); continue;
            }
            auto found = m_entries.find( coord ); if( found != m_entries.end() && found->second.Failed ) continue;
            if( found == m_entries.end() || !found->second.Tile )
            {
                ++m_stats.Pending;
                if( found != m_entries.end() || m_job.valid() || !allowLoad || !m_stats.CoverageReady || !fits( m_tileCost.CpuBytes * limits.Instances, 0 ) ) continue;
                auto& entry = m_entries[coord]; entry.Active = entry.Loading = true; entry.Request = ++m_request; entry.LastDemand = frame;
                const auto archive = m_archive; const auto loader = m_loader; const auto generation = entry.Request;
                try { m_job = std::async( std::launch::async, [archive, loader, coord, generation]() { Completion result; result.Coordinate = coord; result.Request = generation; try { if( !loader( *archive, coord, result.Tile, result.Error ) ) result.Tile.reset(); } catch( const std::exception& e ) { result.Error = e.what(); } return result; } ); }
                catch( const std::exception& e ) { entry.Active = entry.Loading = false; entry.Failed = true; m_stats.Error = e.what(); ++m_stats.Failed; }
                changed = true; Measure(); continue;
            }
            auto& entry = found->second;
            if( task.Demand.Visual && !entry.Visual && fits( 0, m_tileCost.GpuBytes * limits.Instances ) ) { entry.Visual = true; changed = true; Measure(); }
            if( task.Demand.Visual && entry.Visual && !entry.UseDetail ) { entry.UseDetail = true; changed = true; }
            if( task.Demand.Collision && !entry.Collision && m_stats.CollisionReserved <= limits.CollisionBytes && m_tileCost.CollisionBytes * limits.Instances <= limits.CollisionBytes - m_stats.CollisionReserved ) { entry.Collision = true; changed = true; Measure(); }
        }
        m_publishDirty |= changed;
        if( m_publishDirty ) m_publishDirty = !PublishPaged( demand );
        m_stats.CollisionReady = m_stats.CoverageReady; m_stats.HorizonReady = true;
        for( const auto& [coord, request] : demand )
        {
            if( request.Collision )
            {
                const auto entry = m_entries.find( coord );
                if( entry == m_entries.end() || !entry->second.Active || !entry->second.Tile || !entry->second.Collision || std::find( m_snapshot->CollisionTiles.begin(), m_snapshot->CollisionTiles.end(), entry->second.Tile->Collision ) == m_snapshot->CollisionTiles.end() ) m_stats.CollisionReady = false;
            }
            if( request.Coverage && !std::binary_search( m_snapshot->CoverageTiles.begin(), m_snapshot->CoverageTiles.end(), coord ) ) m_stats.HorizonReady = false;
        }
        Measure();
        if( !m_stats.CoverageReady ) m_stats.Pressure = "Paged terrain metadata exceeds the configured CPU/GPU budget";
        else if( !m_stats.HorizonReady ) m_stats.Pressure = "Loading the coarse terrain horizon; existing coverage retires before its reservations can be reused";
    }
}
