#pragma once
#include "Terrain/ResidentTerrain.h"
#include <atomic>
#include <functional>
#include <future>

namespace TerrainData
{
    // One owner-thread controller and one owned CPU worker. No worker touches a live document.
    class TerrainPreview
    {
    public:
        using Builder = std::function<bool( const Heightfield&, uint64_t, const std::shared_ptr<const ResidentTerrain>&,
            std::shared_ptr<const ResidentTerrain>&, std::string&, const std::atomic_bool* )>;
        struct Statistics { uint64_t Started = 0, Published = 0, Discarded = 0; double LastMilliseconds = 0; };
        explicit TerrainPreview( Builder InBuilder = UpdateResidentTerrain );
        ~TerrainPreview();
        TerrainPreview( const TerrainPreview& ) = delete;
        TerrainPreview& operator=( const TerrainPreview& ) = delete;
        bool Request( const Heightfield& InField, uint64_t InGUID, std::shared_ptr<const ResidentTerrain>& InOutPreview, std::string& OutError );
        bool Tick( const Heightfield& InField, uint64_t InGUID, std::shared_ptr<const ResidentTerrain>& InOutPreview, std::string& OutError );
        // Explicit readiness barrier for Play and deterministic automation, never the brush loop.
        bool Wait( const Heightfield& InField, uint64_t InGUID, std::shared_ptr<const ResidentTerrain>& InOutPreview, std::string& OutError );
        // Invalidates even a replacement source with the same GUID and local revision counter.
        void Reset();
        bool IsBusy() const { return m_requested || m_job.valid(); }
        const Statistics& GetStatistics() const { return m_stats; }
    private:
        struct Result
        {
            uint64_t Epoch = 0, GUID = 0, Revision = 0;
            std::shared_ptr<const ResidentTerrain> Data;
            std::string Error;
            double Milliseconds = 0;
        };
        Builder m_builder;
        std::future<Result> m_job;
        std::shared_ptr<std::atomic_bool> m_cancel;
        uint64_t m_epoch = 0, m_jobRevision = 0;
        uint64_t m_failedEpoch = UINT64_MAX, m_failedGUID = 0, m_failedRevision = 0;
        bool m_requested = false;
        Statistics m_stats;
    };
}
