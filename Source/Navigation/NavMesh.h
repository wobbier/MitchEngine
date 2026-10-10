#pragma once
#include "Navigation/NavigationTypes.h"
#include "Navigation/NavMeshBuilder.h"
#include "Math/Bounds.h"
#include <functional>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

// A loaded navmesh (Detour) and its query object. Queries run on the main thread; the agents of a
// navmesh live in NavigationCore's crowd for it.
class NavMesh
{
public:
    NavMesh();
    ~NavMesh();
    NavMesh( const NavMesh& ) = delete;
    NavMesh& operator=( const NavMesh& ) = delete;

    bool Load( const NavMeshData& InData );
    void Clear();
    bool IsValid() const { return m_mesh != nullptr; }

    // Shortest path as corner points (start and end included). Partial when the end is unreachable:
    // the path then ends at the reachable point closest to it.
    NavPathStatus FindPath( const Vector3& InStart, const Vector3& InEnd, std::vector<Vector3>& OutCorners, const NavQueryFilter& InFilter = {} ) const;
    // The closest navmesh point within InMaxDistance of InPoint.
    bool SamplePosition( const Vector3& InPoint, float InMaxDistance, NavMeshHit& OutHit, const NavQueryFilter& InFilter = {} ) const;
    // Walks the surface from InStart towards InEnd; true (with the wall in OutHit) when blocked.
    bool Raycast( const Vector3& InStart, const Vector3& InEnd, NavMeshHit& OutHit, const NavQueryFilter& InFilter = {} ) const;
    // A random point on the navmesh, area-weighted.
    bool GetRandomPoint( Vector3& OutPoint, const NavQueryFilter& InFilter = {} ) const;
    // A random point reachable from InCenter within about InRadius.
    bool GetRandomPointAround( const Vector3& InCenter, float InRadius, Vector3& OutPoint, const NavQueryFilter& InFilter = {} ) const;

    // Debug geometry: every surface triangle (with its NavAreas index), outline edges (walls and
    // ledges), and off-mesh links.
    void ForEachTriangle( const std::function<void( const Vector3&, const Vector3&, const Vector3&, uint8_t )>& InVisitor ) const;
    // Polygon edges: InVisitor( a, b, isBoundary ). Boundary edges are walls and ledges; inner edges
    // separate polygons (each reported once).
    void ForEachEdge( const std::function<void( const Vector3&, const Vector3&, bool )>& InVisitor ) const;
    void ForEachLink( const std::function<void( const Vector3&, const Vector3&, float, bool )>& InVisitor ) const;

    int GetTileCount() const;
    int GetPolyCount() const;
    AABB GetBounds() const { return m_bounds; }
    const NavMeshData& GetData() const { return m_data; }
    dtNavMesh* GetDetourMesh() const { return m_mesh; }
    dtNavMeshQuery* GetQuery() const { return m_query; }

    // Detour filter for a mask, with the area costs from Project Settings.
    static void MakeFilter( const NavQueryFilter& InFilter, dtQueryFilter& OutFilter );
    // Search box used to snap points onto the mesh.
    Vector3 GetSnapExtents() const;

private:
    dtNavMesh* m_mesh = nullptr;
    dtNavMeshQuery* m_query = nullptr;
    NavMeshData m_data;
    AABB m_bounds;
};
