#pragma once
#include "ECS/Core.h"
#include "ECS/CoreDetail.h"
#include "Navigation/NavMesh.h"
#include "Navigation/NavMeshBuilder.h"
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class NavMeshSurface;
class NavMeshAgent;
class NavMeshObstacle;
class Transform;
class dtCrowd;

// Navigation (Recast / Detour). Engine-owned: always present.
//
// - Every NavMeshSurface has a navmesh: loaded from its baked file, or baked on load (BakeOnLoad).
//   Bake() gathers the static geometry on the main thread and runs Recast on worker threads; the
//   new navmesh swaps in when it's done, and tools builds save it next to the scene.
// - Every NavMeshAgent joins the crowd of the surface it stands on. While the world runs, path
//   requests are processed, the crowd steps (avoidance, link traversal) and agents move.
// - Queries (FindPath, SamplePosition, Raycast, random points) work in edit mode too.
class NavigationCore final
    : public Core<NavigationCore>
{
public:
    NavigationCore();
    ~NavigationCore() override;

    void Init() final;
    void OnStart() final;
    void OnStop() final;
    void OnRemovedFromWorld() final;
    void OnEntityRemoved( Entity& InEntity ) final;
    void Update( const UpdateContext& inUpdateContext ) final;
    // Loads navmeshes (and swaps in finished bakes) now, so scripts' OnStart can query them.
    void SyncNow();

    // Baking
    bool Bake( NavMeshSurface& InSurface, bool InAsync = true );
    void BakeAll( bool InAsync = true );
    void ClearNavMesh( NavMeshSurface& InSurface, bool InDeleteFile = true );
    bool IsBaking() const;
    bool IsBaking( const NavMeshSurface& InSurface ) const;
    float GetBakeProgress( const NavMeshSurface& InSurface ) const;
    // Blocks until every running bake has finished and swapped in.
    void WaitForBakes();
    // The geometry and markup a surface would bake.
    NavBuildInput GatherInput( NavMeshSurface& InSurface );
    // Project-local path of the surface's navmesh file.
    std::string GetDataPath( const NavMeshSurface& InSurface ) const;

    struct SurfaceInfo
    {
        bool Loaded = false;
        bool Baking = false;
        float Progress = 0.f;
        int Tiles = 0;
        int Polygons = 0;
        float BakeMilliseconds = 0.f;
        int Agents = 0;
        std::string DataPath;
        std::string LastError;
    };
    SurfaceInfo GetSurfaceInfo( const NavMeshSurface& InSurface ) const;

    // Queries use the navmesh under (or nearest to) InStart / InPoint.
    NavMesh* GetNavMesh( const Vector3& InNear ) const;
    NavMesh* GetNavMesh( const NavMeshSurface& InSurface ) const;
    NavPathStatus FindPath( const Vector3& InStart, const Vector3& InEnd, std::vector<Vector3>& OutCorners, const NavQueryFilter& InFilter = {} ) const;
    bool SamplePosition( const Vector3& InPoint, float InMaxDistance, NavMeshHit& OutHit, const NavQueryFilter& InFilter = {} ) const;
    bool Raycast( const Vector3& InStart, const Vector3& InEnd, NavMeshHit& OutHit, const NavQueryFilter& InFilter = {} ) const;
    bool GetRandomPoint( Vector3& OutPoint, const NavQueryFilter& InFilter = {} ) const;
    bool GetRandomPointAround( const Vector3& InCenter, float InRadius, Vector3& OutPoint, const NavQueryFilter& InFilter = {} ) const;

    // Dynamic obstacles carved into the navmeshes (while the world runs).
    size_t GetObstacleCount() const { return m_obstacles.size(); }
    bool IsCarving() const;
    // The baked navmesh no longer matches the scene or the surface's settings (re-checked at most
    // every few seconds; the inspector calls it).
    bool IsOutOfDate( const NavMeshSurface& InSurface, bool InRecheckNow = false );

    size_t GetSurfaceCount() const { return m_surfaces.size(); }
    size_t GetAgentCount() const { return m_agents.size(); }

    // Draw navmeshes, links and agent paths (Scene View > View > Navigation). A surface whose
    // inspector is open draws regardless.
    static inline bool DebugDrawEnabled = false;
    // The inspector of this surface is open this frame (editor).
    void MarkInspected( const NavMeshSurface& InSurface );

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    struct BakeJob
    {
        std::thread Thread;
        std::atomic<bool> Done{ false };
        std::atomic<bool> Cancel{ false };
        std::atomic<int> TilesDone{ 0 };
        std::atomic<int> TilesTotal{ 0 };
        NavBuildResult Result;
        std::string SavePath;   // project-local; empty = keep in memory only
    };

    struct SurfaceRecord
    {
        EntityHandle Surface;
        std::unique_ptr<NavMesh> Mesh;
        dtCrowd* Crowd = nullptr;
        float CrowdRadius = 0.f;
        std::unique_ptr<BakeJob> Bake;
        std::string LoadedPath;
        bool LoadAttempted = false;
        bool Inspected = false;
        float BakeMilliseconds = 0.f;
        std::string LastError;
        std::vector<uint32_t> FilterMasks;  // crowd query filter slot -> area mask
        // Dynamic obstacles: the static geometry (gathered once), the navmesh as baked, the
        // running tile rebuild and the tiles waiting for the next one.
        std::unique_ptr<NavBuildInput> CarveInput;
        NavBuildSettings CarveSettings;
        NavMeshData BaseData;
        bool Carved = false;
        std::unique_ptr<BakeJob> Carve;
        std::set<std::pair<int, int>> PendingTiles;
        // Out-of-date check (inspector)
        double OutOfDateCheckedAt = -1.0;
        bool OutOfDate = false;
    };

    struct ObstacleRecord
    {
        EntityHandle Obstacle;
        NavBuildInput::Volume Current;  // where it is now
        NavBuildInput::Volume Carved;   // where the navmesh has it
        bool HasCarved = false;
        bool Dirty = true;
        float StillTime = 0.f;
        bool Seen = false;
    };

    struct AgentRecord
    {
        EntityHandle Agent;
        uint64_t Surface = 0;
        int CrowdIndex = -1;
        uint64_t ParamsHash = 0;
    };

    void SyncSurfaces();
    void SyncObstacles( float InDeltaSeconds );
    void MarkTiles( const NavBuildInput::Volume& InVolume );
    void StartCarves();
    void PollCarves();
    void RestoreBakedMeshes();
    static NavBuildInput::Volume ObstacleFootprint( NavMeshObstacle& InObstacle, Transform& InTransform );
    void SyncAgents();
    void PollBakes();
    void LoadSurface( SurfaceRecord& InRecord, NavMeshSurface& InSurface );
    void SetMesh( SurfaceRecord& InRecord, const NavMeshData& InData );
    void DestroyCrowd( SurfaceRecord& InRecord );
    void EnsureCrowd( SurfaceRecord& InRecord, float InRadius );
    void RemoveAgent( AgentRecord& InAgent );
    bool AddAgent( AgentRecord& InAgent, NavMeshAgent& InComponent, Entity& InEntity );
    void StepAgents( float InDeltaSeconds );
    void DrawDebug();
    void CancelBake( SurfaceRecord& InRecord );
    SurfaceRecord* FindSurface( const NavMeshSurface& InSurface );
    const SurfaceRecord* FindSurface( const NavMeshSurface& InSurface ) const;
    SurfaceRecord* SurfaceFor( const Vector3& InPosition, float InAgentRadius );
    int FilterSlot( SurfaceRecord& InRecord, uint32_t InAreaMask );

    std::unordered_map<uint64_t, SurfaceRecord> m_surfaces;   // by surface entity id
    std::unordered_map<uint64_t, AgentRecord> m_agents;       // by agent entity id
    std::unordered_map<uint64_t, ObstacleRecord> m_obstacles; // by obstacle entity id
    bool m_running = false;
};

ME_REGISTER_CORE( NavigationCore )
