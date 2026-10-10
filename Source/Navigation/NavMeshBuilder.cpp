#include "PCH.h"
#include "NavMeshBuilder.h"

#include "Recast.h"
#include "DetourAlloc.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

namespace
{
    constexpr uint32_t kFileMagic = ( 'M' << 24 ) | ( 'N' << 16 ) | ( 'A' << 8 ) | 'V';
    constexpr uint32_t kFileVersion = 1;
    // 32-bit poly refs: tile and polygon index bits share 22 bits (the rest is the salt).
    constexpr int kMaxTileBits = 14;

    uint64_t HashBytes( uint64_t InHash, const void* InData, size_t InSize )
    {
        const uint8_t* bytes = static_cast<const uint8_t*>( InData );
        for( size_t i = 0; i < InSize; ++i )
        {
            InHash = ( InHash ^ bytes[i] ) * 1099511628211ull;
        }
        return InHash;
    }

    template<typename T>
    uint64_t HashValue( uint64_t InHash, const T& InValue )
    {
        return HashBytes( InHash, &InValue, sizeof( T ) );
    }

    // NavAreas index -> Recast / Detour area id. Recast reserves 0 for "not walkable".
    unsigned char ToRecastArea( uint8_t InArea )
    {
        return InArea == NavAreas::NotWalkable ? RC_NULL_AREA : static_cast<unsigned char>( ( InArea % NavAreas::kCount ) + 1 );
    }

    int NextPow2( int InValue )
    {
        int value = 1;
        while( value < InValue )
        {
            value <<= 1;
        }
        return value;
    }

    int Log2( int InValue )
    {
        int bits = 0;
        while( ( 1 << ( bits + 1 ) ) <= InValue )
        {
            ++bits;
        }
        return bits;
    }

    // Everything a tile build shares, prepared once.
    struct BakeContext
    {
        const NavBuildInput* Input = nullptr;
        rcConfig Config{};              // per-tile config template (bounds filled per tile)
        std::vector<float> Vertices;    // flat xyz
        std::vector<unsigned char> TriangleAreas;
        std::vector<std::vector<int>> TileTriangles;   // triangle indices per tile (with border)
        std::vector<float> LinkVertices;
        std::vector<float> LinkRadii;
        std::vector<unsigned char> LinkDirections;
        std::vector<unsigned char> LinkAreas;
        std::vector<unsigned short> LinkFlags;
        std::vector<unsigned int> LinkIds;
        float BoundsMin[3] = {};
        float BoundsMax[3] = {};
        int TilesX = 0;
        int TilesZ = 0;
        int MaxPolysPerTile = 0;
        float TileWorldSize = 0.f;
        const NavBuildSettings* Settings = nullptr;
    };

    struct TileResult
    {
        std::vector<uint8_t> Data;
        int PolyCount = 0;
        std::string Error;
    };

    // RAII for the Recast intermediates of one tile.
    struct TileScratch
    {
        rcHeightfield* Solid = nullptr;
        rcCompactHeightfield* Compact = nullptr;
        rcContourSet* Contours = nullptr;
        rcPolyMesh* PolyMesh = nullptr;
        rcPolyMeshDetail* Detail = nullptr;

        ~TileScratch()
        {
            rcFreeHeightField( Solid );
            rcFreeCompactHeightfield( Compact );
            rcFreeContourSet( Contours );
            rcFreePolyMesh( PolyMesh );
            rcFreePolyMeshDetail( Detail );
        }
    };

    TileResult BuildTile( const BakeContext& InBake, int InTileX, int InTileZ )
    {
        TileResult result;
        const std::vector<int>& triangles = InBake.TileTriangles[InTileZ * InBake.TilesX + InTileX];
        if( triangles.empty() )
        {
            return result;
        }

        rcContext context( false );
        rcConfig config = InBake.Config;
        config.bmin[0] = InBake.BoundsMin[0] + InTileX * InBake.TileWorldSize;
        config.bmin[1] = InBake.BoundsMin[1];
        config.bmin[2] = InBake.BoundsMin[2] + InTileZ * InBake.TileWorldSize;
        config.bmax[0] = InBake.BoundsMin[0] + ( InTileX + 1 ) * InBake.TileWorldSize;
        config.bmax[1] = InBake.BoundsMax[1];
        config.bmax[2] = InBake.BoundsMin[2] + ( InTileZ + 1 ) * InBake.TileWorldSize;
        config.bmin[0] -= config.borderSize * config.cs;
        config.bmin[2] -= config.borderSize * config.cs;
        config.bmax[0] += config.borderSize * config.cs;
        config.bmax[2] += config.borderSize * config.cs;

        auto fail = [&]( const char* InStep ) {
            result.Error = "tile " + std::to_string( InTileX ) + "," + std::to_string( InTileZ ) + ": " + InStep;
            result.Data.clear();
            return result;
        };

        TileScratch scratch;
        scratch.Solid = rcAllocHeightfield();
        if( !scratch.Solid || !rcCreateHeightfield( &context, *scratch.Solid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch ) )
        {
            return fail( "out of memory (heightfield)" );
        }

        std::vector<int> tileIndices;
        std::vector<unsigned char> tileAreas;
        tileIndices.reserve( triangles.size() * 3 );
        tileAreas.reserve( triangles.size() );
        for( int triangle : triangles )
        {
            tileIndices.push_back( InBake.Input->Indices[triangle * 3 + 0] );
            tileIndices.push_back( InBake.Input->Indices[triangle * 3 + 1] );
            tileIndices.push_back( InBake.Input->Indices[triangle * 3 + 2] );
            tileAreas.push_back( InBake.TriangleAreas[triangle] );
        }
        if( !rcRasterizeTriangles( &context, InBake.Vertices.data(), static_cast<int>( InBake.Vertices.size() / 3 ), tileIndices.data(), tileAreas.data(), static_cast<int>( tileAreas.size() ), *scratch.Solid, config.walkableClimb ) )
        {
            return fail( "rasterize" );
        }

        const NavBuildSettings& settings = *InBake.Settings;
        if( settings.FilterLowHangingObstacles )
        {
            rcFilterLowHangingWalkableObstacles( &context, config.walkableClimb, *scratch.Solid );
        }
        if( settings.FilterLedgeSpans )
        {
            rcFilterLedgeSpans( &context, config.walkableHeight, config.walkableClimb, *scratch.Solid );
        }
        if( settings.FilterLowHeightSpans )
        {
            rcFilterWalkableLowHeightSpans( &context, config.walkableHeight, *scratch.Solid );
        }

        scratch.Compact = rcAllocCompactHeightfield();
        if( !scratch.Compact || !rcBuildCompactHeightfield( &context, config.walkableHeight, config.walkableClimb, *scratch.Solid, *scratch.Compact ) )
        {
            return fail( "compact heightfield" );
        }
        rcFreeHeightField( scratch.Solid );
        scratch.Solid = nullptr;

        if( !rcErodeWalkableArea( &context, config.walkableRadius, *scratch.Compact ) )
        {
            return fail( "erode" );
        }

        for( const NavBuildInput::Volume& volume : InBake.Input->Volumes )
        {
            if( volume.Outline.size() < 3 )
            {
                continue;
            }
            std::vector<float> outline;
            outline.reserve( volume.Outline.size() * 3 );
            for( const Vector3& point : volume.Outline )
            {
                outline.insert( outline.end(), { point.x, point.y, point.z } );
            }
            int pointCount = static_cast<int>( volume.Outline.size() );
            if( volume.Inflate > 0.f )
            {
                // Bevelled corners add up to a few points per corner.
                std::vector<float> grown( outline.size() * 4 );
                pointCount = rcOffsetPoly( outline.data(), pointCount, volume.Inflate, grown.data(), static_cast<int>( grown.size() / 3 ) );
                outline.swap( grown );
            }
            if( pointCount >= 3 )
            {
                rcMarkConvexPolyArea( &context, outline.data(), pointCount, volume.MinY, volume.MaxY, ToRecastArea( volume.Area ), *scratch.Compact );
            }
        }

        switch( settings.Partition )
        {
        case NavPartition::Monotone:
            if( !rcBuildRegionsMonotone( &context, *scratch.Compact, config.borderSize, config.minRegionArea, config.mergeRegionArea ) )
            {
                return fail( "monotone regions" );
            }
            break;
        case NavPartition::Layers:
            if( !rcBuildLayerRegions( &context, *scratch.Compact, config.borderSize, config.minRegionArea ) )
            {
                return fail( "layer regions" );
            }
            break;
        case NavPartition::Watershed:
        default:
            if( !rcBuildDistanceField( &context, *scratch.Compact ) || !rcBuildRegions( &context, *scratch.Compact, config.borderSize, config.minRegionArea, config.mergeRegionArea ) )
            {
                return fail( "watershed regions" );
            }
            break;
        }

        scratch.Contours = rcAllocContourSet();
        if( !scratch.Contours || !rcBuildContours( &context, *scratch.Compact, config.maxSimplificationError, config.maxEdgeLen, *scratch.Contours ) )
        {
            return fail( "contours" );
        }
        if( scratch.Contours->nconts == 0 )
        {
            return result;  // nothing walkable here
        }

        scratch.PolyMesh = rcAllocPolyMesh();
        if( !scratch.PolyMesh || !rcBuildPolyMesh( &context, *scratch.Contours, config.maxVertsPerPoly, *scratch.PolyMesh ) )
        {
            return fail( "polygons" );
        }
        scratch.Detail = rcAllocPolyMeshDetail();
        if( !scratch.Detail || !rcBuildPolyMeshDetail( &context, *scratch.PolyMesh, *scratch.Compact, config.detailSampleDist, config.detailSampleMaxError, *scratch.Detail ) )
        {
            return fail( "detail mesh" );
        }

        rcPolyMesh& mesh = *scratch.PolyMesh;
        if( mesh.npolys == 0 )
        {
            return result;
        }
        if( mesh.nverts >= 0xffff )
        {
            return fail( "too many vertices; lower TileSize" );
        }
        if( mesh.npolys > InBake.MaxPolysPerTile )
        {
            return fail( "too many polygons; lower TileSize" );
        }
        // Detour area = Recast area (NavAreas index + 1); flags = the area's mask bit, so an area
        // mask is directly the query's include flags.
        for( int i = 0; i < mesh.npolys; ++i )
        {
            const int area = mesh.areas[i];
            mesh.flags[i] = area > 0 ? static_cast<unsigned short>( 1u << ( ( area - 1 ) % NavAreas::kCount ) ) : 0;
        }

        dtNavMeshCreateParams params{};
        params.verts = mesh.verts;
        params.vertCount = mesh.nverts;
        params.polys = mesh.polys;
        params.polyAreas = mesh.areas;
        params.polyFlags = mesh.flags;
        params.polyCount = mesh.npolys;
        params.nvp = mesh.nvp;
        params.detailMeshes = scratch.Detail->meshes;
        params.detailVerts = scratch.Detail->verts;
        params.detailVertsCount = scratch.Detail->nverts;
        params.detailTris = scratch.Detail->tris;
        params.detailTriCount = scratch.Detail->ntris;
        if( !InBake.LinkAreas.empty() )
        {
            params.offMeshConVerts = InBake.LinkVertices.data();
            params.offMeshConRad = InBake.LinkRadii.data();
            params.offMeshConDir = InBake.LinkDirections.data();
            params.offMeshConAreas = InBake.LinkAreas.data();
            params.offMeshConFlags = InBake.LinkFlags.data();
            params.offMeshConUserID = InBake.LinkIds.data();
            params.offMeshConCount = static_cast<int>( InBake.LinkAreas.size() );
        }
        params.walkableHeight = settings.AgentHeight;
        params.walkableRadius = settings.AgentRadius;
        params.walkableClimb = settings.AgentMaxClimb;
        params.tileX = InTileX;
        params.tileY = InTileZ;
        params.tileLayer = 0;
        rcVcopy( params.bmin, mesh.bmin );
        rcVcopy( params.bmax, mesh.bmax );
        params.cs = config.cs;
        params.ch = config.ch;
        params.buildBvTree = true;

        unsigned char* navData = nullptr;
        int navDataSize = 0;
        if( !dtCreateNavMeshData( &params, &navData, &navDataSize ) )
        {
            return fail( "Detour tile data" );
        }
        result.Data.assign( navData, navData + navDataSize );
        result.PolyCount = mesh.npolys;
        dtFree( navData );
        return result;
    }
}


uint64_t NavBuildSettings::Hash() const
{
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue( hash, AgentRadius );
    hash = HashValue( hash, AgentHeight );
    hash = HashValue( hash, AgentMaxClimb );
    hash = HashValue( hash, AgentMaxSlope );
    hash = HashValue( hash, CellSize );
    hash = HashValue( hash, CellHeight );
    hash = HashValue( hash, TileSize );
    hash = HashValue( hash, MinRegionSize );
    hash = HashValue( hash, MergeRegionSize );
    hash = HashValue( hash, EdgeMaxLength );
    hash = HashValue( hash, EdgeMaxError );
    hash = HashValue( hash, DetailSampleDistance );
    hash = HashValue( hash, DetailSampleMaxError );
    hash = HashValue( hash, Partition );
    hash = HashValue( hash, FilterLowHangingObstacles );
    hash = HashValue( hash, FilterLedgeSpans );
    hash = HashValue( hash, FilterLowHeightSpans );
    return hash;
}


void NavBuildInput::AddTriangle( const Vector3& InA, const Vector3& InB, const Vector3& InC, uint8_t InArea )
{
    const int base = static_cast<int>( Vertices.size() );
    Vertices.push_back( InA );
    Vertices.push_back( InB );
    Vertices.push_back( InC );
    Indices.insert( Indices.end(), { base, base + 1, base + 2 } );
    Areas.push_back( InArea );
}


uint64_t NavBuildInput::Hash() const
{
    uint64_t hash = 1469598103934665603ull;
    for( const Vector3& vertex : Vertices )
    {
        hash = HashValue( hash, vertex.x );
        hash = HashValue( hash, vertex.y );
        hash = HashValue( hash, vertex.z );
    }
    hash = HashBytes( hash, Indices.data(), Indices.size() * sizeof( int ) );
    hash = HashBytes( hash, Areas.data(), Areas.size() );
    for( const Volume& volume : Volumes )
    {
        for( const Vector3& point : volume.Outline )
        {
            hash = HashValue( hash, point.x );
            hash = HashValue( hash, point.z );
        }
        hash = HashValue( hash, volume.MinY );
        hash = HashValue( hash, volume.MaxY );
        hash = HashValue( hash, volume.Area );
        hash = HashValue( hash, volume.Inflate );
    }
    for( const Link& link : Links )
    {
        hash = HashValue( hash, link.Start.x );
        hash = HashValue( hash, link.Start.y );
        hash = HashValue( hash, link.Start.z );
        hash = HashValue( hash, link.End.x );
        hash = HashValue( hash, link.End.y );
        hash = HashValue( hash, link.End.z );
        hash = HashValue( hash, link.Radius );
        hash = HashValue( hash, link.Bidirectional );
        hash = HashValue( hash, link.Area );
    }
    if( Bounds.IsValid() )
    {
        hash = HashValue( hash, Bounds.Min.x );
        hash = HashValue( hash, Bounds.Min.y );
        hash = HashValue( hash, Bounds.Min.z );
        hash = HashValue( hash, Bounds.Max.x );
        hash = HashValue( hash, Bounds.Max.y );
        hash = HashValue( hash, Bounds.Max.z );
    }
    return hash;
}


NavBuildResult BuildNavMesh( const NavBuildInput& InInput, const NavBuildSettings& InSettings, const NavBuildOptions& InOptions )
{
    const auto start = std::chrono::steady_clock::now();
    NavBuildResult result;
    const int triangleCount = static_cast<int>( InInput.Indices.size() / 3 );
    if( triangleCount == 0 || InInput.Vertices.empty() )
    {
        result.Error = "no geometry to bake (add colliders or meshes under the surface)";
        return result;
    }
    if( InInput.Areas.size() != static_cast<size_t>( triangleCount ) )
    {
        result.Error = "one area per triangle expected";
        return result;
    }

    BakeContext bake;
    bake.Input = &InInput;
    bake.Settings = &InSettings;
    bake.Vertices.reserve( InInput.Vertices.size() * 3 );
    AABB geometryBounds;
    for( const Vector3& vertex : InInput.Vertices )
    {
        bake.Vertices.insert( bake.Vertices.end(), { vertex.x, vertex.y, vertex.z } );
        geometryBounds.Encapsulate( vertex );
    }
    AABB bounds = geometryBounds;
    if( InOptions.Grid )
    {
        // Same grid as the navmesh being patched (its tiles must line up).
        bounds.Min = Vector3( InOptions.Grid->Origin[0], InOptions.Grid->Origin[1], InOptions.Grid->Origin[2] );
        bounds.Max.y = std::max( bounds.Max.y, bounds.Min.y );
    }
    else if( InInput.Bounds.IsValid() )
    {
        bounds.Min = Vector3( std::max( bounds.Min.x, InInput.Bounds.Min.x ), std::max( bounds.Min.y, InInput.Bounds.Min.y ), std::max( bounds.Min.z, InInput.Bounds.Min.z ) );
        bounds.Max = Vector3( std::min( bounds.Max.x, InInput.Bounds.Max.x ), std::min( bounds.Max.y, InInput.Bounds.Max.y ), std::min( bounds.Max.z, InInput.Bounds.Max.z ) );
        if( !bounds.IsValid() )
        {
            result.Error = "the bake volume doesn't overlap any geometry";
            return result;
        }
    }
    bake.BoundsMin[0] = bounds.Min.x;
    bake.BoundsMin[1] = bounds.Min.y;
    bake.BoundsMin[2] = bounds.Min.z;
    bake.BoundsMax[0] = bounds.Max.x;
    bake.BoundsMax[1] = bounds.Max.y + InSettings.AgentHeight;   // room to stand on the highest surface
    bake.BoundsMax[2] = bounds.Max.z;

    // Areas: slopes steeper than AgentMaxSlope are never walkable.
    const float walkableNormalY = std::cos( std::clamp( InSettings.AgentMaxSlope, 0.f, 89.9f ) * 3.14159265f / 180.f );
    bake.TriangleAreas.resize( triangleCount );
    for( int i = 0; i < triangleCount; ++i )
    {
        const Vector3& a = InInput.Vertices[InInput.Indices[i * 3 + 0]];
        const Vector3& b = InInput.Vertices[InInput.Indices[i * 3 + 1]];
        const Vector3& c = InInput.Vertices[InInput.Indices[i * 3 + 2]];
        const Vector3 normal = ( b - a ).Cross( c - a );
        const float length = normal.Length();
        const bool walkable = length > 0.f && normal.y / length > walkableNormalY;
        bake.TriangleAreas[i] = walkable ? ToRecastArea( InInput.Areas[i] ) : RC_NULL_AREA;
    }

    // Recast config shared by every tile.
    const float cs = std::max( InSettings.GetCellSize(), 0.01f );
    const float ch = std::max( InSettings.GetCellHeight(), 0.01f );
    rcConfig& config = bake.Config;
    config.cs = cs;
    config.ch = ch;
    config.walkableSlopeAngle = InSettings.AgentMaxSlope;
    config.walkableHeight = static_cast<int>( std::ceil( InSettings.AgentHeight / ch ) );
    config.walkableClimb = static_cast<int>( std::floor( InSettings.AgentMaxClimb / ch ) );
    config.walkableRadius = static_cast<int>( std::ceil( InSettings.AgentRadius / cs ) );
    config.maxEdgeLen = static_cast<int>( InSettings.EdgeMaxLength / cs );
    config.maxSimplificationError = InSettings.EdgeMaxError;
    config.minRegionArea = static_cast<int>( InSettings.MinRegionSize * InSettings.MinRegionSize );
    config.mergeRegionArea = static_cast<int>( InSettings.MergeRegionSize * InSettings.MergeRegionSize );
    config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    config.tileSize = std::clamp( InSettings.TileSize, 16, 1024 );
    config.borderSize = config.walkableRadius + 3;
    config.width = config.tileSize + config.borderSize * 2;
    config.height = config.tileSize + config.borderSize * 2;
    config.detailSampleDist = InSettings.DetailSampleDistance < 0.9f ? 0.f : cs * InSettings.DetailSampleDistance;
    config.detailSampleMaxError = ch * InSettings.DetailSampleMaxError;

    int gridWidth = 0;
    int gridHeight = 0;
    rcCalcGridSize( bake.BoundsMin, bake.BoundsMax, cs, &gridWidth, &gridHeight );
    bake.TilesX = std::max( 1, ( gridWidth + config.tileSize - 1 ) / config.tileSize );
    bake.TilesZ = std::max( 1, ( gridHeight + config.tileSize - 1 ) / config.tileSize );
    bake.TileWorldSize = config.tileSize * cs;
    int tileBits = Log2( NextPow2( bake.TilesX * bake.TilesZ ) );
    if( InOptions.Grid )
    {
        // Cover the original grid (geometry may have shrunk since) and keep its reference layout.
        bake.TileWorldSize = InOptions.Grid->TileWidth;
        int maxX = bake.TilesX;
        int maxZ = bake.TilesZ;
        for( const auto& [x, z] : InOptions.OnlyTiles )
        {
            maxX = std::max( maxX, x + 1 );
            maxZ = std::max( maxZ, z + 1 );
        }
        bake.TilesX = maxX;
        bake.TilesZ = maxZ;
        tileBits = Log2( InOptions.Grid->MaxTiles );
        bake.BoundsMax[0] = bake.BoundsMin[0] + bake.TilesX * bake.TileWorldSize;
        bake.BoundsMax[2] = bake.BoundsMin[2] + bake.TilesZ * bake.TileWorldSize;
    }
    const int tileCount = bake.TilesX * bake.TilesZ;
    if( tileBits > kMaxTileBits )
    {
        result.Error = "the area needs " + std::to_string( tileCount ) + " tiles (max " + std::to_string( 1 << kMaxTileBits ) + "); raise TileSize or CellSize";
        return result;
    }
    const int polyBits = 22 - tileBits;
    bake.MaxPolysPerTile = InOptions.Grid ? InOptions.Grid->MaxPolysPerTile : 1 << polyBits;

    // Bucket triangles into the tiles their XZ bounds touch (with the border every tile reads).
    bake.TileTriangles.resize( tileCount );
    const float border = config.borderSize * cs;
    for( int i = 0; i < triangleCount; ++i )
    {
        float minX = FLT_MAX;
        float minZ = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxZ = -FLT_MAX;
        for( int corner = 0; corner < 3; ++corner )
        {
            const Vector3& vertex = InInput.Vertices[InInput.Indices[i * 3 + corner]];
            minX = std::min( minX, vertex.x );
            maxX = std::max( maxX, vertex.x );
            minZ = std::min( minZ, vertex.z );
            maxZ = std::max( maxZ, vertex.z );
        }
        const int x0 = std::clamp( static_cast<int>( std::floor( ( minX - border - bake.BoundsMin[0] ) / bake.TileWorldSize ) ), 0, bake.TilesX - 1 );
        const int x1 = std::clamp( static_cast<int>( std::floor( ( maxX + border - bake.BoundsMin[0] ) / bake.TileWorldSize ) ), 0, bake.TilesX - 1 );
        const int z0 = std::clamp( static_cast<int>( std::floor( ( minZ - border - bake.BoundsMin[2] ) / bake.TileWorldSize ) ), 0, bake.TilesZ - 1 );
        const int z1 = std::clamp( static_cast<int>( std::floor( ( maxZ + border - bake.BoundsMin[2] ) / bake.TileWorldSize ) ), 0, bake.TilesZ - 1 );
        if( maxX + border < bake.BoundsMin[0] || minX - border > bake.BoundsMax[0] || maxZ + border < bake.BoundsMin[2] || minZ - border > bake.BoundsMax[2] )
        {
            continue;   // outside the bake volume
        }
        for( int z = z0; z <= z1; ++z )
        {
            for( int x = x0; x <= x1; ++x )
            {
                bake.TileTriangles[z * bake.TilesX + x].push_back( i );
            }
        }
    }

    for( const NavBuildInput::Link& link : InInput.Links )
    {
        bake.LinkVertices.insert( bake.LinkVertices.end(), { link.Start.x, link.Start.y, link.Start.z, link.End.x, link.End.y, link.End.z } );
        bake.LinkRadii.push_back( std::max( link.Radius, 0.05f ) );
        bake.LinkDirections.push_back( link.Bidirectional ? DT_OFFMESH_CON_BIDIR : 0 );
        bake.LinkAreas.push_back( ToRecastArea( link.Area ) );
        bake.LinkFlags.push_back( static_cast<unsigned short>( NavAreas::Bit( link.Area ) ) );
        bake.LinkIds.push_back( link.UserId );
    }

    // Which tiles to build: all, or the requested ones that exist on the grid.
    std::vector<int> work;
    if( InOptions.OnlyTiles.empty() )
    {
        work.resize( tileCount );
        for( int i = 0; i < tileCount; ++i )
        {
            work[i] = i;
        }
    }
    else
    {
        for( const auto& [x, z] : InOptions.OnlyTiles )
        {
            if( x >= 0 && z >= 0 && x < bake.TilesX && z < bake.TilesZ )
            {
                work.push_back( z * bake.TilesX + x );
                result.RebuiltTiles.emplace_back( x, z );
            }
        }
    }
    const int workCount = static_cast<int>( work.size() );

    // Build tiles on dedicated threads (long-running work must not land in a frame's job waits).
    std::vector<TileResult> tiles( tileCount );
    std::atomic<int> nextTile{ 0 };
    if( InOptions.TilesTotal )
    {
        InOptions.TilesTotal->store( workCount );
    }
    auto worker = [&]() {
        for( ;; )
        {
            if( InOptions.Cancel && InOptions.Cancel->load() )
            {
                return;
            }
            const int next = nextTile.fetch_add( 1 );
            if( next >= workCount )
            {
                return;
            }
            const int index = work[next];
            tiles[index] = BuildTile( bake, index % bake.TilesX, index / bake.TilesX );
            if( InOptions.TilesDone )
            {
                InOptions.TilesDone->fetch_add( 1 );
            }
        }
    };
    unsigned int threadCount = InOptions.ThreadCount;
    if( threadCount == 0 )
    {
        const unsigned int hardware = std::thread::hardware_concurrency();
        threadCount = hardware > 1 ? hardware - 1 : 1;
    }
    threadCount = std::max( 1u, std::min<unsigned int>( threadCount, static_cast<unsigned int>( std::max( workCount, 1 ) ) ) );
    std::vector<std::thread> threads;
    for( unsigned int i = 1; i < threadCount; ++i )
    {
        threads.emplace_back( worker );
    }
    worker();
    for( std::thread& thread : threads )
    {
        thread.join();
    }
    if( InOptions.Cancel && InOptions.Cancel->load() )
    {
        result.Error = "cancelled";
        return result;
    }

    NavMeshData& data = result.Data;
    data.Origin[0] = bake.BoundsMin[0];
    data.Origin[1] = bake.BoundsMin[1];
    data.Origin[2] = bake.BoundsMin[2];
    data.TileWidth = bake.TileWorldSize;
    data.TileHeight = bake.TileWorldSize;
    data.MaxTiles = 1 << tileBits;
    data.MaxPolysPerTile = bake.MaxPolysPerTile;
    data.AgentRadius = InSettings.AgentRadius;
    data.AgentHeight = InSettings.AgentHeight;
    data.AgentMaxClimb = InSettings.AgentMaxClimb;
    data.SourceHash = InInput.Hash() ^ ( InSettings.Hash() * 31ull );
    for( TileResult& tile : tiles )
    {
        if( !tile.Error.empty() )
        {
            result.Error = tile.Error;
            result.Data = NavMeshData();
            return result;
        }
        if( !tile.Data.empty() )
        {
            result.PolyCount += tile.PolyCount;
            data.Tiles.push_back( std::move( tile.Data ) );
        }
    }
    result.TileCount = static_cast<int>( data.Tiles.size() );
    if( data.Tiles.empty() && InOptions.OnlyTiles.empty() )
    {
        result.Error = "nothing walkable (check the agent size and slope, and that surfaces face up)";
        return result;
    }
    result.Success = true;
    result.Milliseconds = std::chrono::duration<float, std::milli>( std::chrono::steady_clock::now() - start ).count();
    return result;
}


std::vector<uint8_t> NavMeshData::Serialize() const
{
    std::vector<uint8_t> out;
    auto write = [&out]( const void* InData, size_t InSize ) {
        const uint8_t* bytes = static_cast<const uint8_t*>( InData );
        out.insert( out.end(), bytes, bytes + InSize );
    };
    const uint32_t tileCount = static_cast<uint32_t>( Tiles.size() );
    write( &kFileMagic, 4 );
    write( &kFileVersion, 4 );
    write( Origin, sizeof( Origin ) );
    write( &TileWidth, 4 );
    write( &TileHeight, 4 );
    write( &MaxTiles, 4 );
    write( &MaxPolysPerTile, 4 );
    write( &AgentRadius, 4 );
    write( &AgentHeight, 4 );
    write( &AgentMaxClimb, 4 );
    write( &SourceHash, 8 );
    write( &tileCount, 4 );
    for( const std::vector<uint8_t>& tile : Tiles )
    {
        const uint32_t size = static_cast<uint32_t>( tile.size() );
        write( &size, 4 );
        write( tile.data(), tile.size() );
    }
    return out;
}


bool NavMeshData::Deserialize( const uint8_t* InData, size_t InSize )
{
    *this = NavMeshData();
    size_t offset = 0;
    auto read = [&]( void* OutData, size_t InBytes ) {
        if( offset + InBytes > InSize )
        {
            return false;
        }
        std::memcpy( OutData, InData + offset, InBytes );
        offset += InBytes;
        return true;
    };
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t tileCount = 0;
    if( !read( &magic, 4 ) || magic != kFileMagic || !read( &version, 4 ) || version != kFileVersion )
    {
        return false;
    }
    bool ok = read( Origin, sizeof( Origin ) ) && read( &TileWidth, 4 ) && read( &TileHeight, 4 ) && read( &MaxTiles, 4 ) && read( &MaxPolysPerTile, 4 )
        && read( &AgentRadius, 4 ) && read( &AgentHeight, 4 ) && read( &AgentMaxClimb, 4 ) && read( &SourceHash, 8 ) && read( &tileCount, 4 );
    for( uint32_t i = 0; ok && i < tileCount; ++i )
    {
        uint32_t size = 0;
        ok = read( &size, 4 ) && offset + size <= InSize;
        if( ok )
        {
            Tiles.emplace_back( InData + offset, InData + offset + size );
            offset += size;
        }
    }
    if( !ok )
    {
        *this = NavMeshData();
    }
    return ok;
}


bool NavMeshData::Save( const std::string& InFullPath ) const
{
    std::ofstream file( InFullPath, std::ios::binary | std::ios::trunc );
    if( !file )
    {
        return false;
    }
    const std::vector<uint8_t> bytes = Serialize();
    file.write( reinterpret_cast<const char*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
    return file.good();
}


bool NavMeshData::Load( const std::string& InFullPath )
{
    std::ifstream file( InFullPath, std::ios::binary );
    if( !file )
    {
        return false;
    }
    const std::vector<uint8_t> bytes( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    return Deserialize( bytes.data(), bytes.size() );
}
