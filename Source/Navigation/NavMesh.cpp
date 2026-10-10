#include "PCH.h"
#include "NavMesh.h"

#include "Engine/ProjectSettings.h"

#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

#include <cfloat>
#include <cstring>
#include <random>

namespace
{
    constexpr int kMaxSearchNodes = 4096;
    constexpr int kMaxPathPolys = 512;
    constexpr int kMaxCorners = 512;

    float RandomFloat()
    {
        thread_local std::mt19937 generator( std::random_device{}() );
        return std::uniform_real_distribution<float>( 0.f, 0.99999f )( generator );
    }

    Vector3 ToVector( const float* InValues )
    {
        return Vector3( InValues[0], InValues[1], InValues[2] );
    }

    // NavAreas index of a Detour area id (Recast area = index + 1).
    uint8_t FromDetourArea( unsigned char InArea )
    {
        return InArea > 0 ? static_cast<uint8_t>( ( InArea - 1 ) % NavAreas::kCount ) : NavAreas::NotWalkable;
    }
}


NavMesh::NavMesh()
{
}


NavMesh::~NavMesh()
{
    Clear();
}


void NavMesh::Clear()
{
    if( m_query )
    {
        dtFreeNavMeshQuery( m_query );
        m_query = nullptr;
    }
    if( m_mesh )
    {
        dtFreeNavMesh( m_mesh );
        m_mesh = nullptr;
    }
    m_data = NavMeshData();
    m_bounds = AABB();
}


bool NavMesh::Load( const NavMeshData& InData )
{
    Clear();
    if( InData.IsEmpty() )
    {
        return false;
    }
    dtNavMeshParams params{};
    std::memcpy( params.orig, InData.Origin, sizeof( params.orig ) );
    params.tileWidth = InData.TileWidth;
    params.tileHeight = InData.TileHeight;
    params.maxTiles = InData.MaxTiles;
    params.maxPolys = InData.MaxPolysPerTile;

    m_mesh = dtAllocNavMesh();
    if( !m_mesh || dtStatusFailed( m_mesh->init( &params ) ) )
    {
        Clear();
        return false;
    }
    for( const std::vector<uint8_t>& tile : InData.Tiles )
    {
        // Detour keeps (and frees) the tile memory, which must come from dtAlloc.
        unsigned char* copy = static_cast<unsigned char*>( dtAlloc( tile.size(), DT_ALLOC_PERM ) );
        std::memcpy( copy, tile.data(), tile.size() );
        if( dtStatusFailed( m_mesh->addTile( copy, static_cast<int>( tile.size() ), DT_TILE_FREE_DATA, 0, nullptr ) ) )
        {
            dtFree( copy );
            Clear();
            return false;
        }
        const dtMeshHeader* header = reinterpret_cast<const dtMeshHeader*>( tile.data() );
        m_bounds.Encapsulate( ToVector( header->bmin ) );
        m_bounds.Encapsulate( ToVector( header->bmax ) );
    }
    m_query = dtAllocNavMeshQuery();
    if( !m_query || dtStatusFailed( m_query->init( m_mesh, kMaxSearchNodes ) ) )
    {
        Clear();
        return false;
    }
    m_data = InData;
    return true;
}


void NavMesh::MakeFilter( const NavQueryFilter& InFilter, dtQueryFilter& OutFilter )
{
    const ProjectSettings& settings = ProjectSettings::Get();
    OutFilter.setIncludeFlags( static_cast<unsigned short>( InFilter.AreaMask & NavAreas::All ) );
    OutFilter.setExcludeFlags( 0 );
    for( int area = 0; area < NavAreas::kCount; ++area )
    {
        OutFilter.setAreaCost( area + 1, std::max( settings.NavAreaCosts[area], 0.01f ) );
    }
}


Vector3 NavMesh::GetSnapExtents() const
{
    const float radius = std::max( m_data.AgentRadius, 0.25f );
    const float height = std::max( m_data.AgentHeight, 1.f );
    return Vector3( radius * 2.f, height, radius * 2.f );
}


NavPathStatus NavMesh::FindPath( const Vector3& InStart, const Vector3& InEnd, std::vector<Vector3>& OutCorners, const NavQueryFilter& InFilter ) const
{
    OutCorners.clear();
    if( !m_query )
    {
        return NavPathStatus::Invalid;
    }
    dtQueryFilter filter;
    MakeFilter( InFilter, filter );
    const Vector3 extents = GetSnapExtents();
    dtPolyRef startRef = 0;
    dtPolyRef endRef = 0;
    float startPoint[3];
    float endPoint[3];
    m_query->findNearestPoly( &InStart.x, &extents.x, &filter, &startRef, startPoint );
    m_query->findNearestPoly( &InEnd.x, &extents.x, &filter, &endRef, endPoint );
    if( !startRef || !endRef )
    {
        return NavPathStatus::Invalid;
    }

    dtPolyRef polys[kMaxPathPolys];
    int polyCount = 0;
    const dtStatus status = m_query->findPath( startRef, endRef, startPoint, endPoint, &filter, polys, &polyCount, kMaxPathPolys );
    if( dtStatusFailed( status ) || polyCount == 0 )
    {
        return NavPathStatus::Invalid;
    }
    const bool partial = polys[polyCount - 1] != endRef || dtStatusDetail( status, DT_PARTIAL_RESULT );
    if( polys[polyCount - 1] != endRef )
    {
        // Head for the reachable point closest to the requested end.
        m_query->closestPointOnPoly( polys[polyCount - 1], endPoint, endPoint, nullptr );
    }

    float corners[kMaxCorners * 3];
    int cornerCount = 0;
    m_query->findStraightPath( startPoint, endPoint, polys, polyCount, corners, nullptr, nullptr, &cornerCount, kMaxCorners );
    OutCorners.reserve( cornerCount );
    for( int i = 0; i < cornerCount; ++i )
    {
        OutCorners.push_back( ToVector( &corners[i * 3] ) );
    }
    return partial ? NavPathStatus::Partial : NavPathStatus::Complete;
}


bool NavMesh::SamplePosition( const Vector3& InPoint, float InMaxDistance, NavMeshHit& OutHit, const NavQueryFilter& InFilter ) const
{
    OutHit = NavMeshHit();
    if( !m_query )
    {
        return false;
    }
    dtQueryFilter filter;
    MakeFilter( InFilter, filter );
    const float extents[3] = { InMaxDistance, InMaxDistance, InMaxDistance };
    dtPolyRef ref = 0;
    float nearest[3];
    m_query->findNearestPoly( &InPoint.x, extents, &filter, &ref, nearest );
    if( !ref )
    {
        return false;
    }
    const Vector3 position = ToVector( nearest );
    const float distance = ( position - InPoint ).Length();
    if( distance > InMaxDistance )
    {
        return false;
    }
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    m_mesh->getTileAndPolyByRefUnsafe( ref, &tile, &poly );
    OutHit.Position = position;
    OutHit.Normal = Vector3::Up;
    OutHit.Distance = distance;
    OutHit.Area = FromDetourArea( poly->getArea() );
    OutHit.Hit = true;
    return true;
}


bool NavMesh::Raycast( const Vector3& InStart, const Vector3& InEnd, NavMeshHit& OutHit, const NavQueryFilter& InFilter ) const
{
    OutHit = NavMeshHit();
    if( !m_query )
    {
        return false;
    }
    dtQueryFilter filter;
    MakeFilter( InFilter, filter );
    const Vector3 extents = GetSnapExtents();
    dtPolyRef startRef = 0;
    float startPoint[3];
    m_query->findNearestPoly( &InStart.x, &extents.x, &filter, &startRef, startPoint );
    if( !startRef )
    {
        return false;
    }
    float t = 0.f;
    float normal[3] = { 0.f, 0.f, 0.f };
    dtPolyRef visited[kMaxPathPolys];
    int visitedCount = 0;
    if( dtStatusFailed( m_query->raycast( startRef, startPoint, &InEnd.x, &filter, &t, normal, visited, &visitedCount, kMaxPathPolys ) ) )
    {
        return false;
    }
    const Vector3 start = ToVector( startPoint );
    if( t == FLT_MAX )
    {
        OutHit.Position = InEnd;
        OutHit.Distance = ( InEnd - start ).Length();
        return false;
    }
    Vector3 position = start + ( InEnd - start ) * t;
    if( visitedCount > 0 )
    {
        float height = position.y;
        if( dtStatusSucceed( m_query->getPolyHeight( visited[visitedCount - 1], &position.x, &height ) ) )
        {
            position.y = height;
        }
    }
    OutHit.Position = position;
    OutHit.Normal = ToVector( normal );
    OutHit.Distance = ( position - start ).Length();
    OutHit.Hit = true;
    return true;
}


bool NavMesh::GetRandomPoint( Vector3& OutPoint, const NavQueryFilter& InFilter ) const
{
    if( !m_query )
    {
        return false;
    }
    dtQueryFilter filter;
    MakeFilter( InFilter, filter );
    dtPolyRef ref = 0;
    float point[3];
    if( dtStatusFailed( m_query->findRandomPoint( &filter, RandomFloat, &ref, point ) ) || !ref )
    {
        return false;
    }
    OutPoint = ToVector( point );
    return true;
}


bool NavMesh::GetRandomPointAround( const Vector3& InCenter, float InRadius, Vector3& OutPoint, const NavQueryFilter& InFilter ) const
{
    if( !m_query )
    {
        return false;
    }
    dtQueryFilter filter;
    MakeFilter( InFilter, filter );
    const Vector3 extents = GetSnapExtents();
    dtPolyRef startRef = 0;
    float startPoint[3];
    m_query->findNearestPoly( &InCenter.x, &extents.x, &filter, &startRef, startPoint );
    if( !startRef )
    {
        return false;
    }
    dtPolyRef ref = 0;
    float point[3];
    if( dtStatusFailed( m_query->findRandomPointAroundCircle( startRef, startPoint, InRadius, &filter, RandomFloat, &ref, point ) ) || !ref )
    {
        return false;
    }
    OutPoint = ToVector( point );
    return true;
}


void NavMesh::ForEachTriangle( const std::function<void( const Vector3&, const Vector3&, const Vector3&, uint8_t )>& InVisitor ) const
{
    if( !m_mesh )
    {
        return;
    }
    const dtNavMesh& mesh = *m_mesh;
    for( int tileIndex = 0; tileIndex < mesh.getMaxTiles(); ++tileIndex )
    {
        const dtMeshTile* tile = mesh.getTile( tileIndex );
        if( !tile || !tile->header )
        {
            continue;
        }
        for( int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex )
        {
            const dtPoly& poly = tile->polys[polyIndex];
            if( poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION )
            {
                continue;
            }
            const dtPolyDetail& detail = tile->detailMeshes[polyIndex];
            for( int triangle = 0; triangle < detail.triCount; ++triangle )
            {
                const unsigned char* indices = &tile->detailTris[( detail.triBase + triangle ) * 4];
                Vector3 corners[3];
                for( int corner = 0; corner < 3; ++corner )
                {
                    const float* vertex = indices[corner] < poly.vertCount
                        ? &tile->verts[poly.verts[indices[corner]] * 3]
                        : &tile->detailVerts[( detail.vertBase + indices[corner] - poly.vertCount ) * 3];
                    corners[corner] = ToVector( vertex );
                }
                InVisitor( corners[0], corners[1], corners[2], FromDetourArea( poly.getArea() ) );
            }
        }
    }
}


void NavMesh::ForEachEdge( const std::function<void( const Vector3&, const Vector3&, bool )>& InVisitor ) const
{
    if( !m_mesh )
    {
        return;
    }
    const dtNavMesh& mesh = *m_mesh;
    for( int tileIndex = 0; tileIndex < mesh.getMaxTiles(); ++tileIndex )
    {
        const dtMeshTile* tile = mesh.getTile( tileIndex );
        if( !tile || !tile->header )
        {
            continue;
        }
        for( int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex )
        {
            const dtPoly& poly = tile->polys[polyIndex];
            if( poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION )
            {
                continue;
            }
            for( int edge = 0; edge < poly.vertCount; ++edge )
            {
                // Outer edges have no neighbour. Inner edges are reported by the lower-indexed
                // polygon; edges to the next tile (DT_EXT_LINK) by this tile.
                const unsigned short neighbour = poly.neis[edge];
                const bool boundary = neighbour == 0;
                if( !boundary && !( neighbour & DT_EXT_LINK ) && neighbour - 1 < polyIndex )
                {
                    continue;
                }
                const float* a = &tile->verts[poly.verts[edge] * 3];
                const float* b = &tile->verts[poly.verts[( edge + 1 ) % poly.vertCount] * 3];
                InVisitor( ToVector( a ), ToVector( b ), boundary );
            }
        }
    }
}


void NavMesh::ForEachLink( const std::function<void( const Vector3&, const Vector3&, float, bool )>& InVisitor ) const
{
    if( !m_mesh )
    {
        return;
    }
    const dtNavMesh& mesh = *m_mesh;
    for( int tileIndex = 0; tileIndex < mesh.getMaxTiles(); ++tileIndex )
    {
        const dtMeshTile* tile = mesh.getTile( tileIndex );
        if( !tile || !tile->header )
        {
            continue;
        }
        for( int i = 0; i < tile->header->offMeshConCount; ++i )
        {
            const dtOffMeshConnection& link = tile->offMeshCons[i];
            InVisitor( ToVector( &link.pos[0] ), ToVector( &link.pos[3] ), link.rad, ( link.flags & DT_OFFMESH_CON_BIDIR ) != 0 );
        }
    }
}


int NavMesh::GetTileCount() const
{
    return static_cast<int>( m_data.Tiles.size() );
}


int NavMesh::GetPolyCount() const
{
    if( !m_mesh )
    {
        return 0;
    }
    int count = 0;
    const dtNavMesh& mesh = *m_mesh;
    for( int tileIndex = 0; tileIndex < mesh.getMaxTiles(); ++tileIndex )
    {
        const dtMeshTile* tile = mesh.getTile( tileIndex );
        if( tile && tile->header )
        {
            count += tile->header->polyCount;
        }
    }
    return count;
}
