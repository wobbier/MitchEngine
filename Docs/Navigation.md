# Navigation

Navigation is built on Recast (navmesh generation) and Detour (runtime navmesh, path queries, DetourCrowd agents), compiled from the `ThirdParty/recastnavigation` submodule. The engine-owned `NavigationCore` runs it. The building blocks:

- **Surfaces:** a `NavMeshSurface` bakes a navmesh for one agent size (a named agent type from Project Settings, or its own custom size) from the static geometry around it, and holds it at runtime.
- **Agents:** a `NavMeshAgent` walks the navmesh of the surface it stands on, steering around other agents.
- **Markup:**
  - `NavMeshModifier` leaves geometry out of the bake or gives it another area.
  - `NavMeshModifierVolume` re-marks a box of the navmesh (holes, costly regions).
  - `NavMeshLink` connects two points (jumps, drops, ladders).
- **Obstacles:** a `NavMeshObstacle` carves its footprint out of the navmesh while the game runs, rebuilding only the tiles it touches.
- **Areas:** 16 named areas with traversal costs, in Project Settings > Navigation. Agents and queries choose which areas they may use with a mask.

> Verified against engine commit 8d6769a5, 2026-10-10 (overhaul Wave 4).

## Overview

| Concept | Type | Notes |
|---------|------|-------|
| System | `NavigationCore` | Engine-owned. Loads and bakes navmeshes, runs one crowd per navmesh, answers queries, draws debug views |
| Bake | `BuildNavMesh` (`NavMeshBuilder.h`) | Pure function: world-space triangles + markup + settings in, Detour tiles out. Safe on any thread |
| Baked data | `NavMeshData` | Detour tile blobs and the parameters to rebuild the mesh; saved as `.navmesh` |
| Runtime mesh | `NavMesh` | A loaded `dtNavMesh` and its query object: paths, sampling, raycasts, random points |
| Area | `NavAreas` index 0–15 | 0 Walkable, 1 Not Walkable (never on the navmesh), 2 Jump (default for links). Detour area id = index + 1; poly flags = the area's mask bit |

Units are metres; +Y is up. The navmesh lies on surfaces facing up (normal `cross( b - a, c - a )`, the engine's winding) that are flatter than `AgentMaxSlope`.

## Key Files

| Path | Role |
|------|------|
| `Source/Navigation/NavigationTypes.h` | `NavAreas`, `NavPartition`, `NavBuildSettings`, `NavPathStatus`, `NavMeshHit`, `NavQueryFilter` |
| `Source/Navigation/NavMeshBuilder.h` / `.cpp` | `NavBuildInput`, `BuildNavMesh` (tiled, parallel), `NavMeshData` (serialize, save, load) |
| `Source/Navigation/NavMesh.h` / `.cpp` | Detour runtime wrapper: queries, area-cost filters, debug geometry iteration |
| `Source/Cores/NavigationCore.h` / `.cpp` | The core: gathering scene geometry, async bakes, file load / save, crowds, agent stepping, debug draw |
| `Source/Components/Navigation/NavMeshSurface.h` / `.cpp` | Bake settings, sources, output path; inspector with Bake / Clear |
| `Source/Components/Navigation/NavMeshAgent.h` / `.cpp` | Steering settings and the path API |
| `Source/Components/Navigation/NavMeshModifiers.h` / `.cpp` | `NavMeshModifier`, `NavMeshModifierVolume`, `NavMeshLink`, `NavMeshObstacle`; area / layer choice names |
| `Source/Engine/ProjectSettings.h` | `NavAreaNames`, `NavAreaCosts` (saved in `Assets/Config/ProjectSettings.json`) |
| `Source/Scripting/Bindings/Systems/Navigation.bindings.cpp` | Script API; C# `NavMeshAgent` and `Navigation` in `Modules/ScriptCore/Source` |
| `ThirdParty/RecastNavigation.sharpmake.cs` | Builds Recast + Detour + DetourCrowd as one static library |
| `Tests/Source/NavigationTests.cpp` | Bake, paths, tiles, file round trip, areas and costs, slopes, links, agents and avoidance |

## How It Works

### Baking

`NavigationCore::Bake( surface, async )` runs in two halves:

1. **Gather (main thread):** `GatherInput` walks the world and builds a `NavBuildInput` in world space.
   - Geometry comes from physics colliders (box, sphere, capsule, mesh; triggers skipped), render meshes (static only, not skinned), or both (`UseGeometry`).
   - Entities are skipped when any of these applies:
     - they are inactive, or outside `IncludeLayers`;
     - they are outside the surface (`Collect` = Children);
     - they move: a NavMeshAgent, a CharacterController, or a dynamic / kinematic Rigidbody on them or an ancestor;
     - a `NavMeshModifier` with `IgnoreFromBuild` applies to them.
   - The modifier that applies is the entity's own, or the nearest ancestor's with `ApplyToChildren`. `OverrideArea` sets the area its triangles get; otherwise they get the surface's `DefaultArea`.
   - Mirrored transforms flip triangle winding so surfaces still face up.
   - Modifier volumes become convex XZ outlines (the hull of the box's corners) with a height range, and links become world-space point pairs. `Collect` = Volume clips the bake to the surface's box.
2. **Build (worker threads):** `BuildNavMesh` runs on a `std::thread` with its own tile workers, not the job system: a long tile must never land inside a frame's `ParallelFor` wait.
   - **Tiles:** the area is split into `TileSize`-cell tiles, and triangles are bucketed into the tiles they touch (plus the border every tile reads).
   - **Per tile:** rasterize, filter (low obstacles, ledges, low ceilings), compact, erode by the agent radius, mark volumes, then build regions with the chosen `Partition`. Contours, polygons, detail mesh and Detour tile data follow.
   - **Progress and cancel:** `TilesDone` / `TilesTotal` report progress, and `Cancel` abandons the bake. A new bake of the same surface cancels the running one.
   - **Limits:** 32-bit polygon references share 22 bits between tile and polygon index. Over 16384 tiles, or too many polygons in a tile, fails with a message suggesting a larger `TileSize` or `CellSize`.
3. **Swap (main thread):** `PollBakes` (each update) loads the result into the surface's `NavMesh`, recreates its crowd, and saves the file in tools builds. It logs `NavMesh baked: '<name>' (N tiles, M polygons) in X ms`.

`NavMeshData::SourceHash` combines the input geometry and the settings, so identical inputs produce identical hashes. Recast is deterministic per tile, so re-baking an unchanged scene produces the same file.

### Files and loading

- A surface's navmesh lives in `NavMeshData` when that names a file. Otherwise it is `<scene path without .lvl>.<entity name>.navmesh` next to the scene, e.g. `Assets/Scenes/Tests/NavTest.NavSurface.navmesh`.
- The format is a small header (magic `MNAV`, version, Detour parameters, agent size, source hash) followed by length-prefixed Detour tiles.
- Each update, `SyncNow` loads any surface whose path changed. It also runs before scripts' `OnStart` (`ScriptCore`) and when play starts, so scripts can query the navmesh from their first frame.
- With no file, `BakeOnLoad` bakes when the scene loads. That suits procedural levels and game builds that don't ship the file.
- Renaming the surface entity changes the default file name: re-bake, or set `NavMeshData`.

### Agent types

Project Settings > Navigation lists **agent types**: a name, radius, height, step (max climb) and max slope ("Humanoid" by default, up to 16).

- **Surfaces:** a surface's `AgentType` picks the size it bakes for. 0 (Custom) uses the surface's own `Agent*` fields. The out-of-date check sees size changes, because it hashes the resolved settings.
- **Agents:** an agent's `AgentType` makes it join only surfaces of that type. 0 (Any) keeps the surface under the agent with the closest radius.
- **Queries:** `NavQueryFilter::AgentType` queries that type's navmesh.

Overlapping surfaces for different types (people and vehicles on the same streets) therefore never mix.

### Agents

While the world runs (play mode, game builds), every enabled `NavMeshAgent` on an active entity joins a crowd.

- **Joining:** it joins the crowd of the surface whose navmesh is under it, preferring the closest baked agent radius. Crowd agent parameters follow the component (speed, acceleration, radius, height, avoidance quality, area mask through one of 16 filter slots).
- **Each update:** queued requests are applied, then each crowd steps once (`dtCrowd::update`), then results are written back:
  - **Requests:**
    - `SetDestination` snaps the target to the navmesh and calls `requestMoveTarget`.
    - `Stop` holds the agent with a zero velocity request and keeps the path; `Resume` re-requests it.
    - `Warp` re-adds the agent at the new position.
    - `ResetPath` clears the target.
  - **Write-back:**
    - Position and velocity.
    - Path state: `IsPathPending` while Detour's path queue works, then `Complete` / `Partial`.
    - Remaining distance: exact when the path ends within the crowd's next four corners, otherwise a lower bound through the target.
    - Arrival: within `max( StoppingDistance, 0.1 )`, the agent stops and `HasArrived` turns true.
  - **Transform:** with `UpdatePosition` on, the agent's position plus `BaseOffset` is written to its Transform. With `UpdateRotation` on, it turns towards its velocity about Y at `AngularSpeed`.
  - **Manual movement:** turn `UpdatePosition` off and read `GetDesiredVelocity()` to drive a `CharacterController` or physics body yourself.
- **Links:** DetourCrowd slides agents across off-mesh links. The core adds an arc of `LinkJumpHeight` (0 = straight), and `IsOnLink` reports the traversal.
- **Stopping play:** the crowds are emptied when play stops. Edit mode never moves agents.

### Obstacles (runtime carving)

A `NavMeshObstacle` (box or upright cylinder) is left out of bakes. While the world runs, `SyncObstacles` tracks each obstacle's world footprint, a convex XZ outline with a height range:

1. **Movement:** it counts as moved once any outline point drifts more than `MoveThreshold` from where it was last carved. It is carved again after standing still for `CarveDelay`, so a pushed crate doesn't rebuild tiles every frame.
2. **Tiles:** the tiles under the old and the new footprint, plus a margin of twice the agent radius for the tile borders, join the surface's pending set.
3. **Rebuild:** `StartCarves` runs `BuildNavMesh` on a background thread for just those tiles. It uses the navmesh's own grid (`NavBuildOptions::Grid` / `OnlyTiles`), the static geometry gathered once, and every obstacle as a Not Walkable volume grown by the agent radius (`rcOffsetPoly`).
4. **Swap:** `PollCarves` replaces the tiles in place (`NavMesh::ReplaceTiles`). DetourCrowd notices the changed polygons and replans paths through them.

Stopping play restores the navmesh as baked. A new bake re-carves every obstacle.

### Queries

`NavigationCore::FindPath`, `SamplePosition`, `Raycast`, `GetRandomPoint` and `GetRandomPointAround` pick the navmesh under the query point. `NavMesh` offers the same queries on a specific mesh.

- **Paths** are corner lists, start and end included. Up to 512 polygons are searched per query.
- **Filters:** a `NavQueryFilter` mask selects areas. Costs come from Project Settings when the query runs, so changing a cost affects the next path. Its `AgentType` picks that type's navmesh rather than the one under the point.
- **Raycast** walks the surface. It reports the first wall with its normal, and the hit's height comes from the polygon it stopped on.

### Editor

- **Inspector:** the `NavMeshSurface` inspector has Bake / Clear (Clear also deletes the file), a progress bar while baking, tile / polygon / agent counts, the last error, and the output path. It warns when the bake is **out of date**: every few seconds, `IsOutOfDate` hashes what a bake would gather now (plus the settings) and compares it with the file's `SourceHash`. A surface draws its navmesh while its inspector is open.
- **Debug view:** Scene View > View > Navigation (action `View.Navigation`) draws every navmesh, editor view only:
  - a translucent fill coloured by area;
  - bright outline edges (walls and ledges) and faint polygon edges;
  - links as arcs;
  - link and modifier-volume gizmos, and obstacle footprints (red once carved, yellow while waiting to carve);
  - agents' radius circles and destinations.
- **Baking everything:** `Navigation.BakeAll` (command palette) bakes every surface.
- **Create menu:** Create > Navigation adds a surface, agent, link, modifier volume or obstacle.
- **Project Settings > Navigation** names areas 2–15 and sets every area's cost. Areas 0 and 1 are fixed.
- **Inspector fields:** area and mask fields show area names, because reflected int fields can declare `Choices` / `MaskChoices`.
- **Debug draw:** `DebugDraw` gained filled triangles (`Triangle`, `Triangles`). They are alpha blended, depth tested, and never written to depth.

### Scripts

The C# API covers the agent and the queries:

- `NavMeshAgent`: `SetDestination`, `IsStopped`, `ResetPath`, `Warp`, `HasPath`, `HasArrived`, `RemainingDistance`, `Velocity`, `DesiredVelocity` and `Speed`.
- `Navigation`: `FindPath` (returns a corner array), `SamplePosition`, `Raycast` and `GetRandomPoint`.

`Assets/Scripts/Tests/NavProbe.cs` is an example.

## How to Extend

- **Enemies that chase the player:** put a `NavMeshAgent` on the enemy and call `SetDestination( player position )` every few frames. Re-requesting every frame works, but restarts the path search each time.
- **A costly area (water, mud):** name an area in Project Settings > Navigation and give it a cost above 1. Then mark it with a `NavMeshModifierVolume`, or a `NavMeshModifier` on the geometry, and re-bake. Units that must avoid it entirely clear its bit in `AreaMask`.
- **Gaps and ledges:** add a `NavMeshLink` and place its points within `Radius` of the navmesh on each side. Unidirectional links model drops.
- **Runtime rebuilds** (procedural levels): set `BakeOnLoad`, or call `NavigationCore::Bake( surface, true )` after changing the level. Prefer `Monotone` partitioning and smaller tiles for speed.
- **Tests:** build a `NavBuildInput` by hand (`AddTriangle`), then call `BuildNavMesh` and query a `NavMesh`. For agents, put a `NavigationCore` in a `World`, bake synchronously (`Bake( surface, false )`), `Start` the world and call `Update` with a delta.

## Caveats & Fragility

- **Obstacles carve only while the game runs**, and only into navmeshes that are already loaded. Static geometry that moves in the editor still needs a re-bake (the out-of-date warning says when).
- **One navmesh per agent type.** Mixed sizes need a surface per type, each baking the same geometry again. Agents and queries without a type use the surface under them with the closest radius. C# path queries don't take an agent type yet; C# agents can set `AgentType` through field access.
- **The crowd holds 256 agents per navmesh**, and 16 distinct area masks per crowd (more fall back to the first filter).
- **Agents don't push or collide with physics.** With `UpdatePosition` on they are moved kinematically. Pair them with a CharacterController (manual mode) for collisions.
- **Remaining distance is a lower bound** when the path bends more than four corners ahead.
- **The out-of-date check gathers the scene.** It is throttled to every few seconds while a surface's inspector is open, but it costs a full gather on very large scenes.
- **Detour queries run on the main thread.** Long `FindPath` calls in a loop are a frame cost; DetourCrowd's internal path queue spreads agent path searches over frames.
- **Win64 / macOS** builds are written but unverified; the library is plain C++.

## Related Docs

- `Docs/Cores-and-Components-Reference.md`: where `NavigationCore` sits among the cores
- `Docs/Architecture.md`: the frame loop (navigation runs after game update, before animation)
- `Docs/Physics.md`: colliders (the default bake source), CharacterController
- `Docs/Scripting-DotNet.md`: the script API
- `Docs/Editor-Havana.md`: Project Settings, View menu, edscript commands
