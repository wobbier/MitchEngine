#pragma once
#include "Terrain/TerrainStreamArchive.h"
#include <future>
#include <functional>
#include <map>

namespace TerrainData
{
    struct StreamLimits
    {
        uint64_t CpuBytes = 256ull << 20, GpuBytes = 128ull << 20, CollisionBytes = 512ull << 20;
        uint32_t HoldFrames = 30;
        uint32_t Instances = 1;
    };

    struct TileDemand
    {
        TileCoord Coordinate;
        bool Collision = false, Visual = false;
        double Priority = 0; // Smaller first; collision always precedes visual requests.
        bool Coverage = false;
    };

    struct StreamStats
    {
        uint64_t CpuReserved = 0, GpuReserved = 0, CollisionReserved = 0;
        uint64_t Loaded = 0, Evicted = 0, Cancelled = 0, Failed = 0, BytesRead = 0;
        size_t Resident = 0, Detail = 0, Retiring = 0, Pending = 0;
        size_t QueryPins = 0, DrawPins = 0, CollisionPins = 0;
        size_t CoverageResident = 0, CoverageRetiring = 0, CoveragePending = 0;
        uint64_t CoverageLoaded = 0, CoverageEvicted = 0;
        bool HorizonReady = true;
        bool Loading = false, CollisionReady = true, CoverageReady = true;
        std::string Pressure, Error;
    };

    // Per-asset cache. The main thread owns scheduling/publication. A single owned worker reads
    // and prepares one independently bounded tile; it never joins the frame-helping job queue.
    // Snapshots and geometry references pin retiring reservations until their consumers release.
    class TerrainStreamCache
    {
    public:
        using Loader = std::function<bool( const StreamArchive&, TileCoord, std::shared_ptr<const StreamTile>&, std::string& )>;
        explicit TerrainStreamCache( std::shared_ptr<const StreamArchive> InArchive, Loader InLoader = ReadStreamTile );
        ~TerrainStreamCache();
        TerrainStreamCache( const TerrainStreamCache& ) = delete;
        TerrainStreamCache& operator=( const TerrainStreamCache& ) = delete;
        void Tick( uint64_t InFrame, std::span<const TileDemand> InDemand, const StreamLimits& InLimits, bool InAllowLoad = true );
        // Drops an unused publication and checks pins before a world releases a retired cache.
        bool CanRetire();
        // Explicit test/loading-screen drain: still performs I/O on the owned worker.
        void WaitForRequest();
        void InvalidatePending();
        std::shared_ptr<const ResidentTerrain> GetData() const { return m_snapshot; }
        const StreamStats& GetStats() const { return m_stats; }
        const StreamArchive& GetArchive() const { return *m_archive; }
        static StreamLimits TileReservation( const Settings& InSettings );
        static StreamLimits CoverageReservation( const Settings& InSettings, uint8_t InBaseLod, bool InSurface = false );

    private:
        friend class TerrainStreamScheduler;
        struct Entry
        {
            std::shared_ptr<const StreamTile> Tile;
            uint64_t LastDemand = 0, LastVisual = 0, Request = 0;
            bool Active = false, Visual = false, UseDetail = false, Collision = false, Loading = false, Failed = false;
        };
        struct Completion
        {
            TileCoord Coordinate;
            uint64_t Request = 0;
            std::shared_ptr<const StreamTile> Tile;
            std::shared_ptr<const StreamCoverageTile> Page;
            bool Coverage = false;
            std::string Error;
        };
        bool HasConsumers( const Entry& InEntry ) const;
        void Measure();
        bool Publish( const std::map<TileCoord, TileDemand>& InDemand );
        void TickPaged( uint64_t InFrame, std::span<const TileDemand> InDemand, const StreamLimits& InLimits, bool InAllowLoad );
        bool PublishPaged( const std::map<TileCoord, TileDemand>& InDemand );
        struct CoverageEntry
        {
            std::shared_ptr<const StreamCoverageTile> Page;
            uint64_t LastDemand = 0, Request = 0;
            bool Active = false, Loading = false, Failed = false;
        };
        bool HasConsumers( const CoverageEntry& InEntry ) const;
        std::shared_ptr<const StreamArchive> m_archive;
        Loader m_loader;
        std::map<TileCoord, Entry> m_entries;
        std::map<TileCoord, CoverageEntry> m_pages;
        std::future<Completion> m_job;
        std::shared_ptr<const ResidentTerrain> m_snapshot;
        std::vector<std::weak_ptr<const ResidentTerrain>> m_snapshots;
        StreamLimits m_limits, m_tileCost, m_baseCost, m_pageCost;
        StreamStats m_stats;
        uint64_t m_request = 0;
        bool m_publishDirty = false;
        std::map<TileCoord, TileDemand> m_publishedDemand;
    };

    // Adds a clipped terrain-local region; no full dataset traversal, including negative tiles.
    void AddStreamDemand( const Settings& InSettings, const Vector3& InLocalPosition, double InRadius,
        bool InCollision, std::map<TileCoord, TileDemand>& InOutDemand, bool InCoverage = false );
}
