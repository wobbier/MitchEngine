# Serialization and Scenes

Scenes and prefabs are JSON in **scene format v2**: a `"Cores"` array naming the systems the scene needs and a flat, depth-first `"Entities"` array where each entity has a 64-bit GUID, an optional `Parent` GUID and a `Components` list. `SceneSerializer` owns both directions; version 1 files (nested `"Scene"`/`"Children"`) are migrated on load. Entity references are stored as GUIDs and remapped when prefabs or copies are instanced.

> Verified against engine commit dab803a2, 2026-10-08.

## Overview

Components serialize through `BaseComponent::Serialize` / `Deserialize`: the sealed wrappers write `"Type"` (the registry name) and `"Enabled"` (when false) and call `OnSerialize` / `OnDeserialize`, which default to **reflection** (`Docs/ECS.md`). Cores implement the same pair. Assets are referenced by project-relative path; `.meta` sidecars carry asset GUIDs (`Docs/Resources-and-Assets.md`).

## Key Files

| Path | Role |
|------|------|
| `Source/World/SceneSerializer.h` / `Source/World/SceneSerializer.cpp` | Format v2, `SerializeEntities`, `SerializeWorld`, `Deserialize`, `MigrateToLatest`, `InstantiatePrefab` |
| `Source/World/Scene.h` / `Source/World/Scene.cpp` | A scene file: `Load`, `Save`, `SaveCopy` |
| `Source/ECS/EntityHandle.h` / `Source/ECS/EntityHandle.cpp` | GUID (de)serialization of `EntityHandle` fields, `SerializationWorldScope` |
| `Source/Engine/World.cpp` | `CreateFromPrefab` → `SceneSerializer::InstantiatePrefab` |
| `Source/ECS/ComponentDetail.h` / `Source/ECS/CoreDetail.h` | Name → factory registries |
| `Modules/Dementia/Source/Reflection/Reflection.h` | Default component (de)serialization |

## How It Works

### Format v2

```json
{
    "Version": 2,
    "Cores": [ { "Type": "SceneGraphTestCore" } ],
    "Entities": [
        { "GUID": "4f1c09a2b37de811", "Name": "Door", "DestroyOnLoad": true,
          "Components": [ { "Type": "Transform", "Position": [0, 0, 0], "Rotation": [0, 0, 0, 1], "Scale": [1, 1, 1] } ] },
        { "GUID": "9c0b2e4d11aa7730", "Name": "Handle", "Parent": "4f1c09a2b37de811", "Active": false, "Layer": 2,
          "Prefab": { "Asset": "Assets/Handle.prefab", "Source": "77e0aa0012345678" },
          "Components": [ { "Type": "Transform", "...": "..." }, { "Type": "Mesh", "...": "..." } ] }
    ]
}
```

- Entities are written depth-first, parents before children; sibling order is preserved.
- `Active` and `Layer` are omitted at their defaults; `DestroyOnLoad` is always written.
- `Transform` rotation is a quaternion `[x, y, z, w]`; three-element Euler degrees from v1 files are still read.
- `EntityHandle` fields serialize as the target's GUID string (or `null`).

### Load (`SceneSerializer::Deserialize`)

1. **Migrate** if the data isn't v2 (`MigrateToLatest`: assigns GUIDs, flattens children, tolerates the single-object `Children` written by the old editor duplicate bug).
2. **Cores**: `World::AddCoreByName` + `Deserialize` for each entry (when `LoadOptions::LoadCores`).
3. **GUIDs**: every entity's GUID is decided up front. Saved GUIDs are kept unless `RemapGUIDs` is set (prefabs, paste, duplicate) or the GUID already exists in the world; a saved→live remap table is built.
4. **Create** every entity (marked loading so `Init` is deferred).
5. **Components**: `AddComponentByName` + `Deserialize` inside a `SerializationWorldScope`, so `EntityHandle` fields resolve through the remap table — forward references work.
6. **Hierarchy**: `Transform::SetParent` to the remapped parent, or to `LoadOptions::Parent` for roots.
7. **Init** every component in file order, then apply `Active` and clear the loading flag. Activation and core membership happen at the next sync point.

`Scene::Load` sets `World::IsLoading` around the call so no sync point sees a half-built scene. Invalid JSON is reported and the load fails cleanly.

### Save

`SceneSerializer::SerializeWorld( world, sceneRoot )` writes everything under the scene root (the `SceneCore` root transform itself is excluded), plus every entity without a `Transform`, plus the serializable cores (editor builds). `SerializeEntities( world, roots )` writes subtrees; parent links that point outside the set are omitted so the result instantiates as roots. `Scene::Save` retargets the scene's path; `Scene::SaveCopy` doesn't (play-mode snapshots, autosave).

### Prefabs

`World::CreateFromPrefab( path, parent )` → `SceneSerializer::InstantiatePrefab`: the file is parsed and migrated once and cached (`ClearPrefabCache`; the engine clears it when a `.prefab` changes on disk), then deserialized with `RemapGUIDs` so every instance gets fresh GUIDs and internal references point inside the instance. Each created entity records its **prefab link** (`EntityRecord::PrefabAsset` / `PrefabSource`), which is saved with the scene. Overrides/apply/revert are editor features (`Docs/Editor-Havana.md`).

### Models

`Model::Init` expands a model file into child entities named after its nodes. When a saved scene already contains those children, `Model::Init` **reuses** the same-named children instead of creating new ones, so edits to model sub-entities (materials, transforms) survive save/load.

## Caveats & Fragility

- **Names are the schema** for types: component, core and material class names link files to code. Renaming one orphans data (one warning per instance at load).
- **Asset references are paths**: `.meta` GUIDs exist (`AssetDatabase`) but components still store paths.
- **Model child reuse is by name**: two same-named sibling nodes in a model map onto the first saved child.
- **`DestroyOnLoad: false` entities survive `World::Unload`** but are written into whatever scene is saved while they're alive.
- **Save is editor-only** (`Scene::Save`); game builds have no save-game path yet.

## Related Docs

- `Docs/ECS.md` — registries, component lifecycle and the loading flag
- `Docs/Architecture.md` — `Engine::LoadScene` orchestration
- `Docs/Resources-and-Assets.md` — asset GUIDs and the prefab cache invalidation
- `Docs/Materials-and-Shaders.md` — the embedded material JSON
- `Docs/Editor-Havana.md` — play-mode snapshots and editor save
