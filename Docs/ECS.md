# ECS

MitchEngine's ECS is an **Entity–Component–Core** design: entities are generational IDs into a slot table of records, components live in per-type paged pools, and *cores* are the systems — each declares a `ComponentFilter` and holds the entities that match it. `World` owns all of it and applies structural changes at **sync points** (`World::Simulate`). This doc covers the machinery, the component lifecycle and the recipes for adding components and cores.

> Verified against engine commit dab803a2, 2026-10-08.

## Overview

- An `Entity` is a lightweight `{World*, EntityID}` view; all per-entity state (name, GUID, activity, layer, component masks, prefab link) lives in the World's `EntityRecord`. Copies held by cores never go stale or diverge.
- Components of one type are stored in a `ComponentPool<T>`: 256-element pages, stable addresses for a component's lifetime, a dense entry list for iteration. No per-component heap allocation or `shared_ptr`.
- Adding a component is immediate; removing components and destroying entities are **deferred** to the next sync point, and core membership is re-evaluated there too. Cores never see their entity lists mutate mid-iteration.
- Type identity comes from `ClassTypeId` (dense, thread-safe, first-request order) — **stable within a run only**; serialization always goes through registry name strings.

## Key Files

| Path | Role |
|------|------|
| `Source/ECS/EntityID.h` | 64-bit ID: 32-bit slot index + 32-bit generation (0 = null); mirrored by `Modules/ScriptCore/Source/Core/Entity.cs` |
| `Source/ECS/Entity.h` / `Source/ECS/Entity.cpp` | `Entity` view: components, activity, name, GUID, layer, loading/DestroyOnLoad flags |
| `Source/ECS/EntityHandle.h` / `Source/ECS/EntityHandle.cpp` | Weak reference (ID + `World*`), GUID (de)serialization, `SerializationWorldScope` |
| `Source/ECS/Component.h` | `BaseComponent` / `Component<T>`: lifecycle hooks, `Enabled`, reflection-driven default JSON |
| `Source/ECS/ComponentPool.h` | `ComponentPool<T>` paged storage + `IComponentPool` |
| `Source/ECS/ComponentTypeArray.h` | Component mask (`kMaxComponentTypes` = 256) |
| `Source/ECS/ComponentFilter.h` | Requires / RequiresOneOf / Excludes |
| `Source/ECS/ComponentDetail.h` | Name-string → factory `ComponentRegistry` (`ME_REGISTER_COMPONENT`) |
| `Source/ECS/Core.h` / `Source/ECS/Core.cpp` | `BaseCore` / `Core<T>`: O(1) membership, priority, Update/FixedUpdate/LateUpdate |
| `Source/ECS/CoreDetail.h` | Name-string → factory `CoreRegistry` (`ME_REGISTER_CORE`) |
| `Source/Engine/World.h` / `Source/Engine/World.cpp` | Entity records, pools, sync points, destruction, cores, queries |
| `Source/Components/Transform.h` / `Source/Components/Transform.cpp` | Hierarchy (parent/children) used for activity inheritance and recursive destroy |
| `Modules/Dementia/Source/Reflection/Reflection.h` | Field reflection used by default component serialization |

## How It Works

### Entity identity

`World::CreateEntity` takes a free slot (or appends one) and returns an `EntityHandle`. Each record keeps the slot's current generation; destroying an entity bumps it, so old `EntityID`s and handles stop resolving (`World::GetRecord` checks index *and* generation). Every entity also gets a random 64-bit **GUID** — the persistent identity used by scenes, prefabs and entity references (`World::FindEntityByGUID`). `World::FindEntityByIDValue` is O(1).

### Components

`Entity::AddComponent<T>(args...)` constructs `T` in `World::GetComponentPool<T>()` and returns the existing instance if the entity already has one. The component's `Parent` handle is set before `Init()` runs. While an entity is *loading* (`Entity::SetLoading(true)`, used by scene/prefab loading) `Init()` is deferred so deserialization can happen first.

`Entity::RemoveComponent<T>()` only marks the component for removal; it stays accessible until the next sync point.

### Component lifecycle

| Hook | When |
|------|------|
| constructor → `Deserialize` → `Init()` | On add (immediately unless loading) |
| `OnEnable()` / `OnDisable()` | When the component's `Enabled` flag or the entity's active-in-hierarchy state flips (activity changes are applied at sync points; `SetEnabled` calls the hook immediately if the entity is active) |
| `OnPropertyChanged( field )` | After a reflected field is written through the reflection API (inspector, undo, scripts) |
| `OnDestroy()` | Right before the component is destroyed (removal or entity destruction) |

A **disabled component doesn't count toward core filters**, so disabling a `Mesh` removes the entity from `RenderCore` at the next sync.

### Activity

`Entity::SetActive` sets *active-self*. The effective *active-in-hierarchy* state also requires every ancestor (via `Transform`) to be active. Inactive entities are removed from every core. Reparenting re-queues the entity for evaluation.

### Cores (systems)

A core declares its filter in its constructor (`Base( ComponentFilter().Requires<A>().Excludes<B>() )`). Membership is a dense `std::vector<Entity>` plus a sparse slot table, so `BaseCore::Contains`, add and remove are O(1) (removal swap-removes, so entity order isn't stable). Cores run in ascending `Priority`, ties broken by name, and get `Update`, `FixedUpdate` (fixed simulation rate, see `Docs/Architecture.md`) and `LateUpdate`. A new core is offered every existing entity at the next sync point.

Two ownership stories:
- **Engine-owned cores** (`CameraCore`, `SceneCore`, `RenderCore`, `ParticleCore`, the physics, animation and audio cores, `UICore`, the editor's `EditorCore`) are added with `World::AddCore( core )`, owned by their creator and updated explicitly by `Engine::Run`.
- **Scene-loaded cores** come from `World::AddCoreByName` (scene `"Cores"` lists). The World owns and frees them, and ticks them from `UpdateLoadedCores` / `FixedUpdateLoadedCores` / `LateUpdateLoadedCores` once `World::Start` has run.

### Sync points (`World::Simulate`)

Structural changes queue the entity; `Simulate` then, for each queued entity: recomputes active-in-hierarchy, computes the effective mask (present & enabled & not pending removal), adds/removes it from every core whose filter result changed (`OnEntityAdded` / `OnEntityRemoved`), fires `OnEnable`/`OnDisable` when activity flipped (and queues children), and destroys pending-removal components. Then it processes the destroy queue: each destroyed root is collected **children first** and destroyed (cores get `OnEntityRemoved` + `OnEntityDestroyed`, components `OnDisable`/`OnDestroy`). `MarkForDelete` is safe to call repeatedly. Callbacks that queue more work are drained in the same call. `Simulate` is a no-op while `World::IsLoading` is set or while the World is iterating (`World::Each`, loaded-core updates).

`Engine::Run` calls `Simulate` between fixed steps, after loaded cores, after the game update and after late update.

### Queries

- `World::Each<A, B>( fn( Entity&, A&, B& ) )` — every live, active entity with all listed components enabled; iterates the first type's pool.
- `World::ForEachEntity`, `FindEntityByGUID`, `FindEntityByName`, `GetEntity`.

## How to Extend

### Add a component

```cpp
#pragma once
#include "ECS/Component.h"

class Health
    : public Component<Health>
{
    ME_REFLECTABLE( Health )
public:
    Health() : Component( "Health" ) {}

    void Init() override {}
    void OnDestroy() override {}

    float Value = 100.f;
    EntityHandle LastAttacker;   // serialized by GUID
};

ME_REGISTER_COMPONENT( Health )
```

```cpp
// Health.cpp
ME_REFLECT_BEGIN( Health )
    ME_FIELD( Value ).Range( 0.f, 100.f );
    ME_FIELD( LastAttacker );
ME_REFLECT_END()
```

Reflected components serialize with no extra code (the default `OnSerialize`/`OnDeserialize` use reflection). Override them only for custom formats. The name passed to `Component( "...")` must match the registered type name.

### Add a core

```cpp
#pragma once
#include "ECS/Core.h"
#include "Components/Health.h"

class RegenCore
    : public Core<RegenCore>
{
public:
    RegenCore() : Base( ComponentFilter().Requires<Health>() ) { Priority = 10; }

    void Update( const UpdateContext& context ) override
    {
        for( Entity& entity : GetEntities() )
        {
            entity.GetComponent<Health>().Value += context.GetDeltaTime();
        }
    }
};

ME_REGISTER_CORE( RegenCore )
```

## Caveats & Fragility

- **Component removal and entity destruction are deferred** — `HasComponent` stays true and the component stays valid until the next sync point.
- **`Entity::GetComponent<T>` asserts on a missing component** and doesn't check liveness; use `TryGetComponent<T>` (returns null) when unsure, and `EntityHandle` for references kept across frames.
- **TypeIds are first-request-ordered** — never persist or compare them across processes; use registry names.
- **`AddComponent<T>` never replaces** — it returns the existing instance and ignores the constructor arguments.
- **Core order within a phase is by `Priority`, then name** — not registration order.
- **The World must outlive its handles and engine-owned cores must outlive the World** (`World::~World` touches every registered core).
- **Up to 256 component types** (`kMaxComponentTypes`); raising it is a one-line change.
- **Registry misses are soft**: unknown component/core names log a warning; scene loads continue without that data.

## Related Docs

- `Docs/Architecture.md` — when sync points and core updates run in the frame
- `Docs/Serialization-and-Scenes.md` — GUIDs, references and how registry names drive loading
- `Docs/Cores-and-Components-Reference.md` — catalog of every existing core and component
- `Docs/Jobs-and-Events.md` — threading rules when cores fan work out to jobs
