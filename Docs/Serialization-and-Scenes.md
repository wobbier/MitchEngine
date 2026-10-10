# Serialization and Scenes

Scenes and prefabs are JSON in **scene format v2**: a `"Cores"` array naming the systems the scene needs and a flat, depth-first `"Entities"` array where each entity has a 64-bit GUID, an optional `Parent` GUID and a `Components` list. `SceneSerializer` owns both directions; version 1 files (nested `"Scene"`/`"Children"`) are migrated on load. Entity references are stored as GUIDs and remapped when prefabs or copies are instanced. Asset references are paths backed by asset GUIDs, so moving or renaming assets doesn't break the files that use them.

> Verified against engine commit 7e869c6e, 2026-10-09; asset references against a76e6e79, 2026-10-10.

## Overview

Components serialize through `BaseComponent::Serialize` / `Deserialize`: the sealed wrappers write `"Type"` (the registry name) and `"Enabled"` (when false) and call `OnSerialize` / `OnDeserialize`, which default to **reflection** (`Docs/ECS.md`). Cores implement the same pair. Assets are referenced by project-relative path, and each saved file also records the GUIDs behind those paths (see Asset references below; `.meta` sidecars carry the GUIDs, `Docs/Resources-and-Assets.md`).

## Key Files

| Path | Role |
|------|------|
| `Source/World/SceneSerializer.h` / `Source/World/SceneSerializer.cpp` | Format v2, `SerializeEntities`, `SerializeWorld`, `Deserialize`, `MigrateToLatest`, `InstantiatePrefab` |
| `Source/World/Scene.h` / `Source/World/Scene.cpp` | A scene file: `Load` (remaps moved assets), `Save`, `SaveCopy` (writes the reference table) |
| `Modules/Dementia/Source/Resource/AssetDatabase.h` | GUID ↔ path, moves with former paths, prefab GUIDs, lazy scans |
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

`World::CreateFromPrefab( path, parent )` → `SceneSerializer::InstantiatePrefab`. `LoadPrefabData` parses and migrates the file once and caches it (`ClearPrefabCache`; the engine clears the cache when a `.prefab` changes on disk). The data is then deserialized with `RemapGUIDs`, so every instance gets fresh GUIDs and internal references point inside the instance. Each created entity records its **prefab link**: `EntityRecord::PrefabAsset` (normalized by `NormalizePrefabPath` to the project-local path) and `PrefabSource` (the entity's GUID in the file). The link is saved with the scene. An entity's own `"Prefab"` key in the data wins over the outer prefab, so nested prefab instances keep linking to their own asset.

`SerializePrefab( world, root, path )` produces a prefab's contents from an instance:

- Entities linked to that prefab get their source GUIDs back, so GUIDs are stable across applies, and their self-links are dropped.
- Parent links and entity references are remapped to match.
- Entities added in the instance keep their own GUIDs.

Overrides, apply, revert and unpack are editor features (`Docs/Editor-Havana.md`).

### Asset references

Components keep asset paths (readable files, simple code), and the files keep the GUIDs behind them. A prefab file looks like this (scenes have the same table, without `AssetGUID`):

```json
{
    "AssetGUID": "c12a54af19ab173a",
    "AssetReferences": {
        "52b08b229bbb9b87": "Assets/Textures/Concrete/ConcreteDiffuse.png",
        "6784e96c32fd1241": "Assets/Models/Synty/ExplorerKit/FBX/SM_Veh_4x4_Car_01.fbx"
    },
    "Version": 2,
    "Entities": [ ... ]
}
```

- **Saving** (`Scene::SaveCopy`, the editor's `WritePrefab`) calls `PrepareForSave`. `CollectAssetReferences` walks every string in the data, and those the `AssetDatabase` knows are recorded by GUID. Only strings with a folder and an extension are looked up. This is generic: custom-serialized components (`Model`, `Mesh` material textures, prefab links, …) are covered without per-component code.
- **Prefabs** also get an `"AssetGUID"` of their own, which is kept across saves. Prefabs have no `.meta`; the key sorts first, so the database scan reads just the top of each `.prefab`.
- **Loading** (`Scene::Load`, `LoadPrefabData`) calls `RemapAssetReferences`. For each entry whose path no longer exists, it asks the database where that GUID lives now and rewrites every occurrence of the old path (logged as `asset '…' moved to '…'`). Saving the file again writes the new paths. Game builds scan the asset metadata lazily, only when an entry is missing, so loads where nothing moved cost nothing extra.
- **In the editor**, moving or renaming in the asset browser updates the database at once (`AssetDatabase::Move`, folders included) and relinks the open scene's prefab instances. The database remembers the former paths: a `Get` of an old path loads from the new one, prefab links to the old path still resolve, and a scene saved before it's reloaded records the asset under its old path (so the next load remaps it). Deleting to the trash unregisters the asset; duplicating a prefab gives the copy a new `AssetGUID`.
- A scan warns when two files share a GUID (copied outside the editor), because references to it are then ambiguous.

### Models

`Model::Init` expands a model file into child entities named after its nodes. When a saved scene already contains those children, `Model::Init` **reuses** the same-named children instead of creating new ones, so edits to model sub-entities (materials, transforms) survive save/load.

## Caveats & Fragility

- **Names are the schema** for types: component, core and material class names link files to code. Renaming one orphans data (one warning per instance at load).
- **Asset references follow moves only for GUID-backed assets.** Assets with a `.meta` (textures, models, audio, shaders, materials, input actions) and prefabs saved since `AssetGUID` was added are tracked. Scenes (`.lvl`) and C# scripts are not; prefabs written before `AssetGUID` existed are tracked once they're saved again. Paths built in code at runtime have no table, but they still resolve within the editor session that moved the asset.
- **The table is written on save.** A file saved before this feature has no `AssetReferences`, so moves made before its next save can't be followed.
- **Model child reuse is by name**: two same-named sibling nodes in a model map onto the first saved child.
- **`DestroyOnLoad: false` entities survive `World::Unload`** but are written into whatever scene is saved while they're alive.
- **Save is editor-only** (`Scene::Save`); game builds have no save-game path yet.

## Related Docs

- `Docs/ECS.md` — registries, component lifecycle and the loading flag
- `Docs/Architecture.md` — `Engine::LoadScene` orchestration
- `Docs/Resources-and-Assets.md` — asset GUIDs and the prefab cache invalidation
- `Docs/Materials-and-Shaders.md` — the embedded material JSON
- `Docs/Editor-Havana.md` — play-mode snapshots and editor save
