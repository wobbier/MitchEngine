#include "PCH.h"
#include "TerrainStreamScheduler.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainData
{
    void TerrainStreamScheduler::Tick( uint64_t frame, std::span<const StreamBatch> batches, const StreamWorldLimits& limits )
    {
        struct Plan
        {
            const StreamBatch* Batch;
            StreamLimits Grant;
            std::vector<TileDemand> Demand;
        };
        struct Candidate { size_t PlanIndex; TileDemand Demand; };
        std::vector<Plan> plans;
        std::vector<Candidate> ranked;
        uint64_t cpu = 0, gpu = 0, collision = 0, coverageCpu = 0, coverageGpu = 0;
        m_stats = {};
        for( const auto& batch : batches )
        {
            auto& cache = *batch.Cache;
            // A departing instance can still have deferred draws/shapes. Keep the peak instance
            // reservation until this entire cache retires rather than releasing it prematurely.
            cache.m_limits.Instances = std::max( cache.m_limits.Instances, batch.Limits.Instances );
            cache.Measure();
            Plan plan{ &batch, batch.Limits, {} }; plan.Grant.Instances = cache.m_limits.Instances;
            const uint64_t instances = plan.Grant.Instances;
            coverageCpu += cache.m_baseCost.CpuBytes * instances; coverageGpu += cache.m_baseCost.GpuBytes * instances;
            const uint64_t fixedCpu = cache.m_baseCost.CpuBytes * instances + cache.m_snapshot->Patches.size() * 128 + 4096;
            plan.Grant.CpuBytes = std::min( batch.Limits.CpuBytes, fixedCpu );
            plan.Grant.GpuBytes = std::min( batch.Limits.GpuBytes, cache.m_baseCost.GpuBytes * instances );
            plan.Grant.CollisionBytes = 0;
            cpu += plan.Grant.CpuBytes; gpu += plan.Grant.GpuBytes;
            if( batch.Active )
            {
                ++m_stats.ActiveAssets;
                std::map<TileCoord, TileDemand> merged;
                for( const auto& request : batch.Demand ) if( cache.m_archive->Config.Contains( request.Coordinate ) && std::isfinite( request.Priority ) )
                {
                    auto [it, added] = merged.emplace( request.Coordinate, request );
                    if( !added ) { it->second.Collision |= request.Collision; it->second.Visual |= request.Visual; it->second.Priority = std::min( it->second.Priority, request.Priority ); }
                }
                for( const auto& [coord, request] : merged ) ranked.push_back( { plans.size(), request } );
            }
            else ++m_stats.RetiringAssets;
            plans.push_back( std::move( plan ) );
        }
        const auto less = [&]( const Candidate& a, const Candidate& b ) {
            if( a.Demand.Collision != b.Demand.Collision ) return a.Demand.Collision;
            if( a.Demand.Priority != b.Demand.Priority ) return a.Demand.Priority < b.Demand.Priority;
            const auto& aa = plans[a.PlanIndex].Batch->Cache->GetArchive(); const auto& bb = plans[b.PlanIndex].Batch->Cache->GetArchive();
            if( aa.AssetGUID != bb.AssetGUID ) return aa.AssetGUID < bb.AssetGUID;
            if( aa.Path != bb.Path ) return aa.Path < bb.Path;
            return a.Demand.Coordinate < b.Demand.Coordinate;
        };
        std::stable_sort( ranked.begin(), ranked.end(), less );
        const auto fits = []( uint64_t used, uint64_t cost, uint64_t cap ) { return used <= cap && cost <= cap - used; };
        for( const auto& candidate : ranked )
        {
            auto& plan = plans[candidate.PlanIndex]; auto& cache = *plan.Batch->Cache;
            const uint64_t instances = plan.Grant.Instances;
            const uint64_t tileCpu = cache.m_tileCost.CpuBytes * instances + cache.m_archive->Config.SamplesPerTile() * 16 + 1024;
            const uint64_t tileGpu = cache.m_tileCost.GpuBytes * instances, tileCollision = cache.m_tileCost.CollisionBytes * instances;
            auto request = candidate.Demand;
            const bool visual = request.Visual && fits( gpu, tileGpu, limits.GpuBytes ) && fits( plan.Grant.GpuBytes, tileGpu, plan.Batch->Limits.GpuBytes );
            const bool decoded = ( request.Collision || visual ) && fits( cpu, tileCpu, limits.CpuBytes ) && fits( plan.Grant.CpuBytes, tileCpu, plan.Batch->Limits.CpuBytes );
            const bool physical = request.Collision && decoded && fits( collision, tileCollision, limits.CollisionBytes ) && fits( plan.Grant.CollisionBytes, tileCollision, plan.Batch->Limits.CollisionBytes );
            if( decoded ) { cpu += tileCpu; plan.Grant.CpuBytes += tileCpu; }
            if( decoded && visual ) { gpu += tileGpu; plan.Grant.GpuBytes += tileGpu; }
            if( physical ) { collision += tileCollision; plan.Grant.CollisionBytes += tileCollision; }
            request.Visual = decoded && visual;
            // Keep unfunded required requests visible to the readiness barrier. Optional demand
            // is removed so a lower-priority asset actually retires its old detail under pressure.
            if( request.Collision || request.Visual ) plan.Demand.push_back( request );
            if( ( candidate.Demand.Visual && !request.Visual ) || ( request.Collision && !physical ) ) ++m_stats.Deferred;
        }
        const auto measure = [&]() {
            m_stats.CpuReserved = m_stats.GpuReserved = m_stats.CollisionReserved = 0; m_stats.Loading = 0;
            m_stats.CollisionReady = true;
            for( const auto& plan : plans )
            {
                auto& cache = *plan.Batch->Cache; cache.Measure(); const auto& stats = cache.GetStats();
                m_stats.CpuReserved += stats.CpuReserved; m_stats.GpuReserved += stats.GpuReserved; m_stats.CollisionReserved += stats.CollisionReserved;
                m_stats.Loading += stats.Loading;
                if( plan.Batch->Active && !stats.CollisionReady ) m_stats.CollisionReady = false;
            }
            m_stats.OverBudget = m_stats.CpuReserved > limits.CpuBytes || m_stats.GpuReserved > limits.GpuBytes || m_stats.CollisionReserved > limits.CollisionBytes;
        };
        measure();
        const bool pressure = m_stats.Deferred || m_stats.OverBudget;
        const bool coverageFits = coverageCpu <= limits.CpuBytes && coverageGpu <= limits.GpuBytes;
        const auto tick = [&]( Plan& plan, bool load ) {
            measure(); auto& cache = *plan.Batch->Cache; const auto& actual = cache.GetStats();
            auto grant = plan.Grant;
            // Never give an asset another asset's live reservation, even when the priority plan
            // would prefer it. In-flight work and old snapshots keep their charge until release.
            grant.CpuBytes = std::min( grant.CpuBytes, actual.CpuReserved + ( limits.CpuBytes > m_stats.CpuReserved ? limits.CpuBytes - m_stats.CpuReserved : 0 ) );
            grant.GpuBytes = std::min( grant.GpuBytes, actual.GpuReserved + ( limits.GpuBytes > m_stats.GpuReserved ? limits.GpuBytes - m_stats.GpuReserved : 0 ) );
            grant.CollisionBytes = std::min( grant.CollisionBytes, actual.CollisionReserved + ( limits.CollisionBytes > m_stats.CollisionReserved ? limits.CollisionBytes - m_stats.CollisionReserved : 0 ) );
            if( !coverageFits ) grant.CpuBytes = grant.GpuBytes = 0;
            if( pressure || !plan.Batch->Active ) grant.HoldFrames = 0;
            cache.Tick( frame, plan.Demand, grant, load && m_stats.Loading < limits.MaxLoads );
        };
        // Poll and publish retirement everywhere before allowing a new request anywhere.
        for( auto& plan : plans ) tick( plan, false );
        std::vector<Candidate> next;
        for( size_t i = 0; i < plans.size(); ++i )
        {
            auto& plan = plans[i]; auto& cache = *plan.Batch->Cache;
            if( cache.m_job.valid() ) continue;
            for( const auto& request : plan.Demand )
            {
                auto entry = cache.m_entries.find( request.Coordinate );
                if( entry == cache.m_entries.end() || ( !entry->second.Tile && !entry->second.Failed ) ) { next.push_back( { i, request } ); break; }
            }
        }
        std::stable_sort( next.begin(), next.end(), less );
        for( const auto& candidate : next ) tick( plans[candidate.PlanIndex], true );
        measure();
        if( m_stats.OverBudget ) m_stats.Pressure = "World terrain limits are exceeded by existing coverage or pinned reservations; new work waits. Increase limits or reduce the dataset/instances.";
        else if( m_stats.Deferred ) m_stats.Pressure = "World or asset limits defer terrain demand; collision takes priority over optional detail.";
    }
}
