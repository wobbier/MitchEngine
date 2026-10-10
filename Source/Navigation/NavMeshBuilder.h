#pragma once
#include "Navigation/NavigationTypes.h"
#include "Math/Bounds.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Baked navmesh: Detour tile blobs plus the parameters to rebuild the dtNavMesh. Saved as a
// .navmesh file next to the scene (NavMeshSurface::NavMeshData).
struct NavMeshData
{
    float Origin[3] = { 0.f, 0.f, 0.f };
    float TileWidth = 0.f;
    float TileHeight = 0.f;
    int MaxTiles = 0;
    int MaxPolysPerTile = 0;
    float AgentRadius = 0.f;
    float AgentHeight = 0.f;
    float AgentMaxClimb = 0.f;
    uint64_t SourceHash = 0;    // inputs + settings at bake time ("out of date" check)
    std::vector<std::vector<uint8_t>> Tiles;

    bool IsEmpty() const { return Tiles.empty(); }
    std::vector<uint8_t> Serialize() const;
    bool Deserialize( const uint8_t* InData, size_t InSize );
    bool Save( const std::string& InFullPath ) const;
    bool Load( const std::string& InFullPath );
};

// World-space geometry and markup to bake (NavigationCore::GatherInput fills this from the scene).
struct NavBuildInput
{
    std::vector<Vector3> Vertices;
    std::vector<int> Indices;           // three per triangle
    std::vector<uint8_t> Areas;         // per triangle: a NavAreas index

    // A prism that re-marks the surface inside it (NavMeshModifierVolume): convex XZ outline.
    struct Volume
    {
        std::vector<Vector3> Outline;
        float MinY = 0.f;
        float MaxY = 0.f;
        uint8_t Area = NavAreas::NotWalkable;
        float Inflate = 0.f;    // grow the outline by this much (obstacles: the agent radius)
    };
    std::vector<Volume> Volumes;

    // Off-mesh connection (NavMeshLink): agents may jump / climb / drop between the two points.
    struct Link
    {
        Vector3 Start;
        Vector3 End;
        float Radius = 0.5f;
        bool Bidirectional = true;
        uint8_t Area = NavAreas::Jump;
        uint32_t UserId = 0;
    };
    std::vector<Link> Links;

    AABB Bounds;    // bake volume; invalid = the geometry's bounds

    void AddTriangle( const Vector3& InA, const Vector3& InB, const Vector3& InC, uint8_t InArea );
    uint64_t Hash() const;
};

struct NavBuildResult
{
    NavMeshData Data;           // partial builds: only the rebuilt tiles that have polygons
    std::vector<std::pair<int, int>> RebuiltTiles;  // partial builds: every tile rebuilt (x, z)
    bool Success = false;
    std::string Error;
    int TileCount = 0;      // tiles with polygons
    int PolyCount = 0;
    float Milliseconds = 0.f;
};

struct NavBuildOptions
{
    unsigned int ThreadCount = 0;           // 0 = hardware threads - 1 (own threads, not the job system)
    std::atomic<bool>* Cancel = nullptr;    // set to abandon the bake
    std::atomic<int>* TilesDone = nullptr;  // progress
    std::atomic<int>* TilesTotal = nullptr;

    // Patching an existing navmesh (dynamic obstacles): build on its grid instead of the geometry's
    // bounds, and only the listed tiles (x, z). An empty list builds every tile.
    const NavMeshData* Grid = nullptr;
    std::vector<std::pair<int, int>> OnlyTiles;
};

// Bakes a tiled navmesh with Recast, building tiles in parallel. Pure function: safe on any thread.
NavBuildResult BuildNavMesh( const NavBuildInput& InInput, const NavBuildSettings& InSettings, const NavBuildOptions& InOptions = {} );
