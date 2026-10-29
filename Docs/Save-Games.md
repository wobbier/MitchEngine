# Save Games

A save game is a snapshot of the running scene in a named slot under `user://Saves`, with its metadata and a thumbnail. `SaveGames` (C++) and `SaveGame` (C#) save, list, load and delete slots.

> Verified against engine PLAY-4, 2026-10-29.

## Overview

- **What's saved.** The scene serializer's snapshot of everything under the scene root, the same snapshot the editor's Play takes. It covers:
  - transforms and components;
  - script fields: public ones, plus non-public fields marked `[Save]`;
  - entity references, by GUID.
- **Where.** Each slot is a folder (letters, digits, `-` and `_`):
  - `Save.json`: `{ "Version", "Meta", "World" }`.
  - `Meta.json`: the metadata alone, for listing slots cheaply.
  - `Thumbnail.png`: the frame the save was taken on.
- **Metadata.** Name, the scene the save was taken in, the UTC date, playtime (seconds of play across sessions) and the product version.
- **Atomic and async.** The snapshot is taken on the main thread, and a worker writes the JSON. Each file goes to a temporary file first and is renamed over the old one: `Save.json` first, then `Meta.json`. A crash mid-write leaves the previous save whole. A save started while another is still writing waits for it.
- **Loading** is queued and runs at the end of the frame. The current scene unloads, and the slot's world loads in its place under the original scene's path (`Engine::LoadSceneFromData`). In the editor's Play, the world restarts as it does after any scene load, and Stop still returns to the edited scene.
- **Versioned.** `SaveGames::Migrate` upgrades older documents through `kMigrations` (none yet; version 1 is the first) and the world through `SceneSerializer::MigrateToLatest`. A newer document loads what this build knows, with a warning.

## Key Files

| Path | Role |
|------|------|
| `Source/Engine/SaveGames.h` / `.cpp` | Slots, the snapshot, the async atomic write, the queued load, playtime, migrations |
| `Source/Components/Scripting/ScriptComponent.cpp` | `s_includeSaveFields`: during a save's snapshot, script fields include `[Save]` ones |
| `Modules/ScriptCore/Source/Inspector.cs` | `FieldReflection.WithSaved`, `ScriptSerializer.Serialize(target, includeSaveFields)` |
| `Modules/ScriptCore/Source/Core/SaveGame.cs` | C# `SaveGame`, `SaveSlot`, `[Save]` |
| `Source/Scripting/Bindings/Systems/SaveGame.bindings.cpp` | The `SaveGame_*` ABI slots |
| `Modules/Havana/Source/Editor/EditorAutomation.cpp` | `save-game`, `load-game`, `delete-save`, `assert-save`, `save-assert` |
| `Tests/Source/SaveGameTests.cpp` | Slot names, the write/read round trip, migrations |

## C#

```csharp
public class Inventory : Script
{
    public int Coins;                 // public: saved (and stored in the scene)
    [Save] private float timePlayed;  // private: saved with save games only
}

SaveGame.Save("slot1", "Chapter 2");
foreach (SaveSlot slot in SaveGame.List())   // newest first: Slot, Name, Scene, Date, Playtime
    ...
SaveGame.Load("slot1");                       // at the end of this frame
SaveGame.Delete("slot1");
float played = SaveGame.Playtime;
```

On load, scripts are created from the saved fields before `OnStart` runs, so `OnStart` sees the restored state.

## Proof

- `Tests/Source/SaveGameTests.cpp`.
- `Assets/Scenes/Showcase/SaveGame01Write` / `SaveGame02Load` run as two launches sharing `--user-dir`:
  - **Launch 1** plays the Playground for 300 frames, edits the Sweeper's speed and saves.
  - **Launch 2** loads the slot. The chasers, the crates and the Sweeper are where they were saved (`save-assert` compares the live field with the slot's), the edited speed is back, and the Spinner's private `[Save]` counter logs that it resumed.

## Caveats & Fragility

- **Only the main scene is saved.** Additive (streamed) scenes and engine state outside the world (time scale, music position) aren't. Physics velocities aren't serialized either: bodies resume at rest.
- **The thumbnail is the whole frame:** in the editor that includes the editor's UI.
- **Field sizes.** A save holds the scene at full size. Large worlds should save less (a game-defined subset is future work, as is Steam Cloud).
- **`[Save]` fields go through reflection** (`ScriptSerializer`), not the CS-9 generated serializers.

## Related Docs

- `Docs/Serialization-and-Scenes.md`: the snapshot format
- `Docs/Player-Settings.md`: the other user:// document, and `--user-dir`
- `Docs/Scripting-DotNet.md`: script fields
