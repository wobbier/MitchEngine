#pragma once
#include "Terrain/TerrainStreamCache.h"

namespace TerrainData
{
    struct StreamWorldLimits
    {
        uint64_t CpuBytes = 256ull << 20, GpuBytes = 128ull << 20, CollisionBytes = 512ull << 20;
        uint32_t MaxLoads = 2;
    };

    struct StreamWorldStats
    {
        uint64_t CpuReserved = 0, GpuReserved = 0, CollisionReserved = 0;
        size_t ActiveAssets = 0, RetiringAssets = 0, Loading = 0, Deferred = 0;
        bool CollisionReady = true, OverBudget = false;
        std::string Pressure;
    };

    struct StreamBatch
    {
        TerrainStreamCache* Cache = nullptr;
        StreamLimits Limits;
        std::vector<TileDemand> Demand;
        bool Active = true;
    };

    // One world-wide allowance above the per-asset ceilings. Plans all demand before polling
    // caches; every admission also accounts for the actual pins/jobs retained by other assets.
    class TerrainStreamScheduler
    {
    public:
        void Tick( uint64_t InFrame, std::span<const StreamBatch> InBatches, const StreamWorldLimits& InLimits );
        const StreamWorldStats& GetStats() const { return m_stats; }
    private:
        StreamWorldStats m_stats;
    };
}
