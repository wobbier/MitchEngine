#pragma once
#include <memory>
namespace TerrainData { struct CollisionTile; }
namespace TerrainPhysics
{
    // Box3D mesh data is world-independent. Creation/destruction use its thread-safe allocator.
    std::shared_ptr<void> Cook( const TerrainData::CollisionTile& InTile );
}
