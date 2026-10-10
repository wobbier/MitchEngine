#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Navigation/NavigationTypes.h"
#include "Math/Vector3.h"
#include <cstdint>
#include <string>

// Which entities a NavMeshSurface bakes.
enum class NavCollectObjects : uint8_t
{
    All = 0,        // every entity in the world
    Children,       // this entity and its descendants
    Volume,         // everything inside VolumeCenter / VolumeSize (local to this entity)
};

// Which geometry of those entities becomes walkable surface.
enum class NavCollectGeometry : uint8_t
{
    PhysicsColliders = 0,   // Box / Sphere / Capsule / Mesh colliders (fast, what bodies collide with)
    RenderMeshes,           // the Mesh triangles
    Both,
};

// Bakes a navmesh (Recast) for agents of one size from the static geometry around it, and holds it
// at runtime (Detour). Bake from the inspector (or NavigationCore::Bake); the result is saved next
// to the scene as "<Scene>.<Entity name>.navmesh" unless NavMeshData names another file. Static
// geometry only: anything with a dynamic or kinematic Rigidbody, a CharacterController or a
// NavMeshAgent is skipped. NavMeshModifier / NavMeshModifierVolume / NavMeshLink refine the bake.
class NavMeshSurface
    : public Component<NavMeshSurface>
{
    ME_REFLECTABLE( NavMeshSurface )
    friend class NavigationCore;
public:
    NavMeshSurface();

    // Agent: a type from Project Settings > Navigation, or 0 = these custom values.
    int AgentType = 0;
    float AgentRadius = 0.5f;
    float AgentHeight = 2.f;
    float AgentMaxClimb = 0.5f;
    float AgentMaxSlope = 45.f;

    // Sources
    NavCollectObjects Collect = NavCollectObjects::All;
    NavCollectGeometry UseGeometry = NavCollectGeometry::PhysicsColliders;
    uint32_t IncludeLayers = 0xFFFFFFFFu;
    int DefaultArea = NavAreas::Walkable;
    Vector3 VolumeCenter = Vector3( 0.f, 0.f, 0.f );
    Vector3 VolumeSize = Vector3( 20.f, 10.f, 20.f );

    // Advanced
    float CellSize = 0.f;           // 0 = AgentRadius / 3
    float CellHeight = 0.f;         // 0 = half the cell size
    int TileSize = 64;
    float MinRegionSize = 8.f;
    float MergeRegionSize = 20.f;
    NavPartition Partition = NavPartition::Watershed;
    float EdgeMaxLength = 12.f;
    float EdgeMaxError = 1.3f;
    float DetailSampleDistance = 6.f;
    float DetailSampleMaxError = 1.f;

    // Output
    std::string NavMeshData;        // empty = "<Scene>.<Entity name>.navmesh" next to the scene
    bool BakeOnLoad = false;        // bake when the scene loads (procedural levels; no file needed)

    NavBuildSettings GetBuildSettings() const;

    // Runtime (NavigationCore)
    bool HasNavMesh() const;
    bool IsBaking() const;
    float GetBakeProgress() const;  // 0..1 while baking
    void Bake();                    // asynchronous; the new navmesh swaps in when done
    void Clear();                   // unloads it and deletes the baked file

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( NavMeshSurface, "Navigation" )
