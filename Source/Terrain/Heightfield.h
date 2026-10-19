#pragma once

#include "Math/Vector3.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Authoritative CPU terrain data. No world, renderer, file I/O, or editor dependencies.
namespace TerrainData
{
    struct Surface;
    struct TileCoord
    {
        int32_t X = 0;
        int32_t Z = 0;
        bool operator==( const TileCoord& ) const = default;
        bool operator<( const TileCoord& InOther ) const;
    };

    struct SampleCoord
    {
        int64_t X = 0;
        int64_t Z = 0;
        bool operator==( const SampleCoord& ) const = default;
    };

    // Inclusive sample bounds; default constructed rectangles are empty.
    struct SampleRect
    {
        SampleCoord Min{ 1, 1 };
        SampleCoord Max{ 0, 0 };
        bool IsEmpty() const;
        void Include( SampleCoord InSample );
    };

    struct Settings
    {
        uint32_t TileQuads = 256;
        uint32_t PatchQuads = 64;
        float SampleSpacing = 1.f;
        float HeightBase = -512.f;
        float HeightRange = 1024.f;
        TileCoord MinTile;
        uint32_t TilesX = 1;
        uint32_t TilesZ = 1;

        bool Validate( std::string& OutError ) const;
        bool Contains( TileCoord InTile ) const;
        bool Contains( SampleCoord InSample ) const;
        SampleCoord MinSample() const;
        SampleCoord MaxSample() const;
        size_t SamplesPerTile() const;
        TileCoord Owner( SampleCoord InSample ) const;
    };

    // Positive divisor; throws for zero/negative divisors. Remainders are always nonnegative.
    int64_t FloorDivide( int64_t InValue, int64_t InDivisor );

    enum class QueryStatus
    {
        Ready,
        Pending,
        Unavailable,
        OutsideBounds,
        InvalidInput
    };

    struct HeightQuery
    {
        QueryStatus Status = QueryStatus::Unavailable;
        float Height = 0.f;                       // Valid only when Status == Ready.
        Vector3 Normal = Vector3( 0.f, 1.f, 0.f ); // Geometric triangle normal.
    };

    struct Tile
    {
        std::vector<float> Heights; // Row-major: z * (TileQuads + 1) + x, including shared edges.
        uint64_t Revision = 0;
        SampleRect DirtySamples;   // Tile-local bounds, expanded for affected vertex normals.
    };

    class Heightfield
    {
    public:
        // Invalid settings throw std::invalid_argument. External file codecs validate first.
        explicit Heightfield( const Settings& InSettings = {} );

        const std::shared_ptr<const Surface>& GetSurface() const { return m_surface; }
        bool SetSurface( std::shared_ptr<const Surface> InSurface, std::string& OutError );
        const Settings& GetSettings() const { return m_settings; }
        uint64_t GetRevision() const { return m_revision; }
        const std::map<TileCoord, Tile>& GetTiles() const { return m_tiles; }
        const Tile* FindTile( TileCoord InTile ) const;

        // Loading requires exact agreement with every already resident shared edge/corner.
        // All mutations are single-threaded; jobs read an immutable copy/snapshot.
        bool LoadTile( TileCoord InTile, std::vector<float> InHeights, std::string& OutError );
        bool CreateFlatTile( TileCoord InTile, float InHeight, std::string& OutError );
        void UnloadTile( TileCoord InTile );
        bool SetPending( TileCoord InTile, bool InPending );
        void ClearDirty();

        QueryStatus GetSample( SampleCoord InSample, float& OutHeight ) const;
        QueryStatus GetNormal( SampleCoord InSample, Vector3& OutNormal ) const;
        // Requires all tiles sharing the sample. Failure leaves every tile unchanged.
        bool SetSample( SampleCoord InSample, float InHeight, std::string& OutError );
        // Terrain-local X/Z in metres, including the finite outer boundary. Never loads data.
        // Each quad is split along (x + 1, z) -- (x, z + 1), exactly as BuildPatch.
        HeightQuery Query( double InX, double InZ ) const;

    private:
        void Invalidate( SampleRect InGlobalRect );
        std::vector<TileCoord> SharingTiles( SampleCoord InSample ) const;
        size_t Index( TileCoord InTile, SampleCoord InSample ) const;

        std::shared_ptr<const Surface> m_surface;
        Settings m_settings;
        uint64_t m_revision = 0;
        std::map<TileCoord, Tile> m_tiles;
        std::set<TileCoord> m_pending;
    };
}
