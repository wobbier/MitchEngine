#include "PCH.h"
#include "TerrainStreamCache.h"
#include <algorithm>

namespace TerrainData
{
    bool TerrainStreamCache::PublishHierarchy( const std::map<TileCoord, TileDemand>& demand, const std::map<uint32_t, TileDemand>& pages )
    {
        const auto ready = [&]( uint32_t id ) {
            if( m_archive->Nodes[id].Root == int32_t( id ) ) return true;
            const auto found = m_pages.find( id ); return found != m_pages.end() && found->second.Active && found->second.Page && !found->second.Failed;
        };
        const auto cut = ReadyCoverageCut( *m_archive, [&]( uint32_t id ) { return pages.contains( id ); }, ready );
        const auto& s = m_archive->Config; const size_t perNode = size_t( s.TileQuads / s.PatchQuads ) * ( s.TileQuads / s.PatchQuads );
        uint64_t bytes = sizeof( ResidentTerrain ) + cut.size() * ( perNode * 128 + 32 );
        for( const auto& [coord, entry] : m_entries ) if( entry.Active && entry.Tile ) bytes += s.SamplesPerTile() * 4 + 256;
        Measure();
        if( bytes > m_limits.CpuBytes || m_stats.CpuReserved > m_limits.CpuBytes - bytes )
        {
            m_snapshot = m_archive->Coverage; m_publishedDemand.clear(); m_stats.Pressure = "Hierarchy publication waits for pinned CPU reservations"; return false;
        }
        auto data = std::make_shared<ResidentTerrain>( *m_archive->Coverage );
        data->Patches.clear(); data->CoveragePatches.clear(); data->CoverageTiles.clear(); data->CoverageNodes = cut;
        auto heights = std::make_shared<Heightfield>( s ); std::string error;
        if( !heights->SetSurface( m_archive->Coverage->Heights->GetSurface(), error ) ) { m_stats.Error = error; return false; }
        for( auto id : cut )
        {
            const auto& node = m_archive->Nodes[id]; data->CoverageTiles.push_back( node.Coordinate );
            const bool root = node.Root == int32_t( id );
            const auto index = root ? size_t( std::lower_bound( m_archive->Roots.begin(), m_archive->Roots.end(), id ) - m_archive->Roots.begin() ) * perNode : 0;
            const auto& source = root ? m_archive->Coverage->CoveragePatches : m_pages.at( id ).Page->Patches;
            if( !root ) data->ResidencyPins.push_back( m_pages.at( id ).Page );
            data->CoveragePatches.insert( data->CoveragePatches.end(), source.begin() + index, source.begin() + index + perNode );
            const auto detail = m_entries.find( node.Coordinate );
            if( !node.Level && detail != m_entries.end() && detail->second.Active && detail->second.Tile && detail->second.UseDetail )
                data->Patches.insert( data->Patches.end(), detail->second.Tile->Patches.begin(), detail->second.Tile->Patches.end() );
            else data->Patches.insert( data->Patches.end(), source.begin() + index, source.begin() + index + perNode );
        }
        for( const auto& [coord, entry] : m_entries ) if( entry.Active && entry.Tile )
        {
            if( !heights->LoadTile( coord, entry.Tile->Heights, error ) ) { m_stats.Error = error; return false; }
            data->ResidencyPins.push_back( entry.Tile ); const auto found = demand.find( coord );
            if( entry.Collision && found != demand.end() && found->second.Collision ) data->CollisionTiles.push_back( entry.Tile->Collision );
        }
        for( const auto& [coord, request] : demand ) if( ( request.Collision || request.Visual || request.Prefetch ) && !heights->FindTile( coord ) )
        {
            const auto entry = m_entries.find( coord ); if( entry == m_entries.end() || !entry->second.Failed ) heights->SetPending( coord, true );
        }
        heights->ClearDirty(); data->Heights = heights; BuildHierarchyNeighbors( *data );
        m_snapshot = data; m_snapshots.push_back( data ); m_publishedDemand = demand; return true;
    }
}
