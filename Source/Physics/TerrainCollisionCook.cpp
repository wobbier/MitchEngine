#include "PCH.h"
#include "TerrainCollisionCook.h"
#include "Terrain/ResidentTerrain.h"
#include "Physics/Box3DUtils.h"

std::shared_ptr<void> TerrainPhysics::Cook( const TerrainData::CollisionTile& tile )
{
    std::vector<b3Vec3> positions; positions.reserve( tile.Positions.size() );
    for( const Vector3& p : tile.Positions ) positions.push_back( Box3DUtils::ToB3( p ) );
    std::vector<int32_t> indices( tile.Indices.begin(), tile.Indices.end() );
    b3MeshDef def{}; def.vertices = positions.data(); def.stride = sizeof( b3Vec3 ); def.vertexCount = static_cast<int>( positions.size() );
    def.indices = indices.data(); def.triangleCount = static_cast<int>( indices.size() / 3 );
    def.weldVertices = true; def.weldTolerance = 0.0001f; def.identifyEdges = true;
    auto* mesh = b3CreateMesh( &def, nullptr, 0 );
    return mesh ? std::shared_ptr<void>( mesh, []( void* p ) { b3DestroyMesh( static_cast<b3MeshData*>( p ) ); } ) : nullptr;
}
