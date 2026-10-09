#pragma once
#include "JSON.h"
#include "ECS/EntityHandle.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class World;
class Transform;
class Entity;

// Scene / prefab / clipboard serialization (scene format v2).
//
// {
//   "Version": 2,
//   "Cores":    [ { "Type": "PhysicsCore", ... } ],
//   "Entities": [                                  // depth-first: parents before children
//     { "GUID": "9f3c...", "Name": "Door", "Parent": "11ab...", "Active": true,
//       "DestroyOnLoad": true, "Layer": 0,
//       "Prefab": { "Asset": "Assets/Door.prefab", "Source": "77e0..." },
//       "Components": [ { "Type": "Transform", ... }, ... ] }
//   ]
// }
//
// Entities are identified by 64-bit GUIDs (hex strings); EntityHandle fields reference entities by
// GUID, so references survive save/load and are remapped when a prefab or clipboard copy is
// instanced. Version 1 files (nested "Scene"/"Children", Euler rotations) are migrated on load.
namespace SceneSerializer
{
    static constexpr int kVersion = 2;

    std::string GUIDToString( uint64_t InGUID );
    // Accepts a hex string or an unsigned number; 0 for anything else.
    uint64_t GUIDFromJson( const json& InValue );

    // Upgrades any supported scene/prefab JSON (v1 scene, v1 prefab object, v2) to the current format.
    json MigrateToLatest( const json& InData );

    // Renames/converts components whose type was replaced (applied to every loaded component).
    json UpgradeComponent( const json& InComponent );

    // Serializes the given entities and all of their descendants. Parent links pointing outside the
    // serialized set are omitted, so the result instantiates as a set of roots.
    json SerializeEntities( World& InWorld, const std::vector<Entity*>& InRoots );

    // Everything under the scene root (excluding the root itself) plus transform-less entities,
    // and the world's serializable cores.
    json SerializeWorld( World& InWorld, Transform* InSceneRoot );

    struct LoadOptions
    {
        // Give every entity a fresh GUID (prefab instances, paste, duplicate) and remap references.
        bool RemapGUIDs = false;
        // Create the cores listed in the data.
        bool LoadCores = true;
        // Parent for entities whose parent isn't part of the data.
        Transform* Parent = nullptr;
        // Recorded on the created entities as their prefab link.
        std::string PrefabAsset;
    };

    // Creates the entities described by (migrated) data. Returns the roots, in order.
    std::vector<EntityHandle> Deserialize( World& InWorld, const json& InData, const LoadOptions& InOptions );

    // Instantiates a prefab asset (cached, migrated once) under InParent.
    EntityHandle InstantiatePrefab( World& InWorld, const std::string& InPrefabPath, Transform* InParent );

    // Parsed, migrated prefab data (cached). Null when the file is missing or invalid.
    std::shared_ptr<const json> LoadPrefabData( const std::string& InPrefabPath );

    // Serializes an instance subtree as the contents of prefab InPrefabPath: entities linked to that
    // prefab get their prefab-source GUIDs back (stable across applies), their links are dropped,
    // and parent/entity references are remapped accordingly. Nested prefab links are kept.
    // OutInstanceToSource receives instance GUID -> GUID in the prefab file for every entity.
    json SerializePrefab( World& InWorld, Entity& InRoot, const std::string& InPrefabPath, std::unordered_map<uint64_t, uint64_t>* OutInstanceToSource = nullptr );

    // Canonical form of a prefab path for links and cache keys (project-local, forward slashes).
    std::string NormalizePrefabPath( const std::string& InPath );

    // Drops cached prefab data (call when prefab files change).
    void ClearPrefabCache();
}
