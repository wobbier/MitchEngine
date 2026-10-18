# Terrain system plan

> Draft proposal, 2026-10-18. Repository integration points reviewed against engine commit `5e92b9b5` and the current working tree. Milestones 1 and 2 are implemented; milestone 3 has authoring, heightmap exchange, recovery and incremental preview checkpoints. LODs and streaming remain proposed work.

Build a tiled heightfield system with separate authoring data, runtime residency, and render patches. Start with a small, fully resident terrain that can be saved and edited, then add patch LODs and bounded streaming without changing how scenes reference terrain.

This plan lives outside `Docs/` because that directory documents implemented behavior. The implemented CPU data foundation is described in [the terrain reference](../Docs/Terrain.md).

**Implementation status:** milestone 1 supplies the CPU data foundation and versioned codecs. Milestone 2 adds background resident assets, a reflected `Terrain` component, an explicitly scheduled `TerrainCore`, internal lit render patches, selection bounds/IDs, and canonical static collision independent of visual geometry. Lifecycle/readiness tests and a four-tile demo cover the integration. Required terrain failures hold 3D physics rather than allowing bodies to fall through missing ground. Milestone 3 now has an initial authoring checkpoint: a dockable icon-based terrain panel, flat creation/placement, raise/lower/smooth/flatten brushes, source triangle picking, cross-tile delta undo, shared immutable previews and generation-based source publication. Save All and unsaved terrain prompts are wired into the editor. The panel also lays out disabled future tools for painting, tiles, foliage, splines, diagnostics and asset builds. A second authoring checkpoint adds RAW Float32/UInt16 import/export, explicit endian/scale/orientation controls and Make Unique with an undoable instance rebind. A third authoring checkpoint adds timed, self-contained recovery snapshots, a restore/discard manager, clean-undo cleanup and conflict-safe restoration across editor restarts. A fourth authoring checkpoint adds undoable brush preferences, incremental immutable preview geometry, preserved GPU patch buffers and shared per-tile collision cooks. Companion-aware asset moves and background authoring jobs remain follow-up authoring work; LOD selection and streaming still follow in later milestones.

## Initial scope and assumptions

- Terrain is a single elevation over local X/Z, with Y up and distances in metres. Caves, overhangs, and vertical structures use ordinary meshes. Voxel terrain would need a different data model.
- The first asset describes a finite rectangular set of tiles. Signed tile coordinates leave room for expansion and later procedural providers.
- Initially support translation with identity world rotation and scale, including inherited transforms. Express terrain size through sample spacing and elevation settings; reject unsupported transforms visibly.
- One scene entity owns a `Terrain` component. Tiles and render patches are internal runtime records, not automatically generated scene entities.
- Begin with imported or flat heightfields and one ordinary lit material. Basic sculpting comes before sophisticated LODs; layer painting follows a dedicated terrain material.
- Target the existing BGFX backends on Linux, Win64, and macOS. Preserve UWP compatibility. Compute shaders, tessellation, sparse textures, and bindless resources are not baseline requirements.
- Runtime terrain deformation, vegetation, erosion, roads, holes, virtual texturing, and world origin rebasing are later features. Stable sample coordinates and tile invalidation should accommodate them without committing to their implementation now.

## Existing engine foundations and gaps

Paths in this table are relative to the engine root. These are current code integration points; proposed additions appear later.

| Foundation | Reuse | Work terrain still needs |
| --- | --- | --- |
| `Source/Cores/Rendering/RenderCore.cpp` | Parallel command preparation, world bounds, camera visibility, stable command handles | Accept terrain patches in the same visibility-index allocation; the current component filter and work items cover meshes/models |
| `Modules/Moonlight/Source/RenderCommands.h` and `Modules/Moonlight/Source/Renderer.cpp` | Mesh submission, lighting, shadows, selection IDs, debug views | Terrain geometry/material binding and, eventually, per-view terrain draw selection |
| `Modules/Moonlight/Source/Graphics/MeshData.h` and `MeshLod.h` | Initial generated geometry, GPU ownership patterns, existing mesh LOD diagnostics | Regular-grid terrain topology and neighbor continuity; meshoptimizer simplification is not a terrain seam policy |
| `Modules/Dementia/Source/Resource/Resource.h` and `ResourceCache.h` | GUID-backed assets, background CPU loading and main-thread finalization | Independently requested terrain pages, residency accounting, priority, cancellation, byte budgets, and incremental uploads |
| `Source/World/SceneStreaming.cpp` | Scene ownership and unload notifications | Terrain page streaming within a scene; loading one additive scene per patch would couple unrelated lifetimes |
| `Source/Cores/PhysicsCore.cpp` | Static triangle-mesh shape creation, queries, safe physics update boundaries | Terrain-owned collision data and revision-aware replacement; existing mesh collision caches are keyed by `MeshData*` |
| `Source/Cores/NavigationCore.cpp` | Triangle gathering and tiled Recast bakes | Explicit terrain source gathering, including data outside the current visual residency set |
| `Modules/Havana/Source/Editor/EditorOperations.h`, `UndoStack.h`, and `Widgets/SceneViewWidget.cpp` | Scene edits, document undo conventions, viewport input and picking | A terrain asset document, brush capture, sample deltas, asset save points, and cross-tile edits |
| `Source/Engine/Engine.cpp` | Explicit engine-owned core lifecycle and frame scheduling | Terrain publication/preparation hooks; merely registering a new core does not establish the required update order |

Three constraints deserve early attention:

1. `RenderCore::Update` currently selects one LOD from the editor or main camera and shares it across passes. Terrain needs an explicit multi-camera policy rather than silently inheriting this limitation.
2. `Engine::Run` currently prepares mesh commands before the background-load pump and the cameras' late updates. Establish the camera snapshot and publication order when integrating terrain; comments describing the intended order are not sufficient.
3. `Assets/Shaders/Lighting.sh` occupies sampler stages 8–15. A four-layer terrain shader cannot bind separate albedo/normal/ORM textures for every layer alongside that lighting layout.

## Ownership and boundaries

These names describe responsibilities; finalize class names during implementation.

| Proposed part | Owns | Must not own |
| --- | --- | --- |
| `Terrain` component | Asset reference, collision/layer settings, LOD and streaming overrides | Height arrays, GPU handles, generated child entities |
| `TerrainAsset` / `TerrainResource` | Versioned manifest, tile directory, material references, immutable source/cooked metadata | Per-camera choices or mutable editor buffers |
| `TerrainDocument` in Havana | Editable source pages, sample revisions, dirty state, undo/redo and save transactions | Game-world physics objects |
| `TerrainCore` | Per-world instances, demand calculation, tile requests, publication and teardown | ImGui or authoring history |
| `TerrainTileCache` | Shared decoded pages, CPU/GPU residency, pin counts and byte accounting | Scene entity creation |
| Terrain render bridge | Patch geometry, command handles, current draw snapshots | Authoritative height samples |
| Physics/navigation adapters | Collision meshes and bake snapshots derived from terrain data | Visual LOD selection |

Keep sample addressing, interpolation, mesh building, border processing, error calculation, and serialization usable without BGFX, ImGui, or a live world. Use immutable snapshots for asynchronous work.

Data identity is `(asset GUID, tile X, tile Z, content revision, representation)`. Instance identity additionally includes the owning entity GUID and a lifetime generation. Two instances may share immutable pages; request cancellation or unloading one instance must not evict pages still pinned by the other.

Editing changes the shared terrain asset and previews in every instance. Offer **Make Unique** for independent authoring. Prefab duplication continues to share the asset unless explicitly made unique; it must not silently fork or copy all height samples.

## Data layout to establish first

Provisional defaults are tuning values, not hard-coded world limits:

| Unit | Initial proposal | Reason |
| --- | --- | --- |
| Sample spacing | 1 m | Straightforward units for the first test terrain |
| Storage tile | 256 × 256 quads, 257 × 257 height samples | Independent I/O and editing unit; 256 m square at the default spacing |
| Render patch | 64 × 64 quads, 65 × 65 vertices | Sixteen independently culled patches per storage tile |
| Patch LOD | Sample stride `2^L`, initially levels 0–5 | Nested vertices and reusable topology; cap the coarsest useful level after profiling |
| Authoring heights | Float32 elevations in metres | Preserve brush precision and exact before/after undo values |
| Cooked heights | UInt16 with one asset-wide base/range, or Float32 when precision requires it | Explicit quantization error and identical decode at shared borders |
| Layer weights | Initially four normalized channels at a declared resolution | Separate shading detail from geometry resolution |

Tile size, patch size, and weight resolution are validated asset settings. Tile quad counts must be divisible by patch quad counts, and patch quad counts must support the chosen power-of-two strides. Changing these settings is an explicit retile/rebuild operation, not an ordinary inspector edit.

**Addressing and borders:** map terrain-local positions to one integer sample lattice. Use mathematical floor division for negative coordinates. Persist shared edge samples in adjacent tiles, but identify edits by global sample coordinate and apply each logical change once. Define a deterministic owner (the positive-side tile, clamped at the finite outer boundary), synchronize all edge/corner copies, and read at least a one-sample neighbor halo for normals. Derived texture pages need gutters at every mip level. Define a consistent outer-edge sampling rule. A border edit invalidates neighboring normals, error bounds, and relevant cooked pages, including neighbors that are not currently rendered.

**Queries:** define height and normal queries against the same triangle split as the finest collision/render surface, rather than assuming bilinear interpolation matches triangle geometry. A nonresident query returns a status such as `Pending` or `Unavailable`; it must never invent height zero or trigger a synchronous load on the frame thread. An explicit offline/editor query may load source pages. Visual coarsening may deviate from collision within the chosen LOD error budget.

**Bounds and errors:** store min/max height per patch and a tile hierarchy for culling. For every LOD, calculate a conservative maximum deviation from its reconstructed triangle surface. Include decoded quantization error and any morph range in bounds/error accounting. Recompute affected records when editing, even before LOD selection uses them.

**Files:** propose a GUID-backed `.terrain` JSON manifest with version, physical dimensions, elevation encoding, tile coordinates, layer references, and relative source-page paths. Keep binary authoring pages beside it in a companion directory. Scenes serialize the asset reference and component settings only. Integrate companion files with asset-browser move, duplicate, delete, and **Make Unique** operations.

Cook to a versioned binary package with a tile/block directory, offsets, byte sizes, checksums, bounds, errors, and independently decodable payloads. Later streamed detail levels must be independently readable; retaining a full tile's CPU vertices while changing its index buffer only reduces drawn triangles. Package headers specify endianness and encoding rather than writing raw C++ structs. Validate sizes and offsets before allocation. Content hashes include source samples, neighbor dependencies, layer/import settings, and cooker version.

Retain float authoring data. With UInt16 cooking, a height range of `R` metres gives a step of `R / 65535` and a rounding error up to half that step. Import/cook reports this bound; values outside the configured range require an explicit range change or a reported clamp policy. Raw height imports declare dimensions, bit depth, byte order, scale, and orientation. Height and weight data bypass sRGB color conversion and ordinary lossy texture import.

## Rendering and LOD progression

Start with CPU-generated patch meshes and the existing lit material path. Build and upload geometry when samples change, not every frame. All patches identify the owning terrain entity for selection; brush raycasts resolve the actual hit against terrain data.

Extend render work-item assembly so terrain and ordinary meshes share one visibility-index namespace. Removing a terrain retires its commands before their referenced geometry is released. Keep off-camera shadow casters available; camera visibility alone cannot determine caster residency.

For the first terrain LOD milestone, use **fixed-footprint patches with power-of-two index strides**. This keeps storage tiles independent of render decisions and avoids introducing quadtree splitting, streaming, and editing simultaneously.

- Choose the coarsest level within a configurable pixel-error threshold. Use per-level geometric error, viewport height, projection type, and a conservative distance to patch bounds; handle near-plane/intersecting-camera cases by retaining detail. Orthographic views need their own projection scale. Add hysteresis to prevent level oscillation.
- Balance edge neighbors so their stride differs by at most a factor of two, including across storage-tile boundaries. Refine the coarser patch when necessary.
- Precompute stitched edge/corner index variants for the four-edge mask. Validate their winding and coverage. Skirts may cover the temporary missing-data perimeter, but are excluded from collision, navigation, and height queries; they are not the internal LOD seam solution.
- Reuse a canonical height-derived normal field across levels to limit lighting seams. Add coordinated geomorphing only after discrete topology is watertight; morphs must agree at shared edges and be identical in forward, shadow, and picking geometry.
- Initially choose the finest level requested by any participating visible camera and draw that common topology in all views. Include caster demand and required neighbor patches. This costs extra detail but avoids the current single-camera limitation. Later move to per-view draw lists with shared resident data, explicit shadow selection, and coordinated seam decisions within each view.
- Reuse shared index buffers where the render abstraction permits it. Account for the initial cost of unique full-resolution vertex buffers and retained `MeshData::CollisionPositions/CollisionIndices` copies; index LOD alone is not a GPU-memory streaming solution.

Once measurements justify it, add hierarchical patch selection/coarse coverage and GPU height displacement from a shared regular grid. Keep the tile format and query model unchanged. Displacement is a deliberate render-path milestone: forward, depth/shadows, picking, wireframe, bounds, and material batch keys must all understand the same height binding and morph parameters.

Geometry clipmaps cache nested moving grids and support smooth transition regions, as described in [the original GPU Gems chapter](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry). [Filip Strugar's CDLOD paper and implementation](https://github.com/fstrugar/CDLOD) provide a quadtree-grid alternative with continuous transitions. They are references for a later renderer decision, not dependencies of the initial fixed-patch design. Revisit that choice if long draw distances make fixed patch counts the limiting cost.

## Streaming and publication

Treat visual, CPU-query, collision, navigation, and editor residency as separate demands over shared tile identities. Culling answers whether to draw; residency answers which representations must remain available.

Use the existing resource system for the manifest, metadata, materials, and its CPU-load/main-thread-finalization pattern. Add a terrain-owned request scheduler for independently budgeted tile blocks; do not assume `ResourceCache::GetAsync` already provides terrain priority, cancellation, or strict residency limits. Share infrastructure where practical without replacing the general resource system.

Each representation moves through `Unloaded → Requested → Loading → ReadyCPU → UploadQueued → Resident → Retiring`. Failed/cancelled requests have explicit outcomes. CPU-only representations can stop at `ReadyCPU`. Every completion carries its request generation and content revision; stale completions are discarded after edits, eviction, scene unload, or Play/Stop.

- Prioritize the gameplay collision safety region, then visible detail, neighbor continuity, predicted movement, and background prefetch. Support editor and game cameras together; cap low-priority auxiliary-camera demand.
- Budget decoded CPU bytes, GPU bytes, collision bytes, in-flight requests, upload bytes per frame, and main-thread finalization time. Charge pending and retiring allocations too. If required pins exhaust the budget, reduce optional visual detail/prefetch or hold the dependent gameplay/editor operation at a readiness barrier. Report budget pressure rather than silently growing allocations or queues.
- Keep blocking I/O on loader threads. Keep long cooks/bakes off the general frame job queue because helping waits can execute queued jobs on a frame-critical thread. Use short bounded jobs for in-memory patch computations.
- The current resource pump's time budget does not preempt a single expensive finalizer. Split terrain GPU uploads and physics publication into measured, bounded units.
- Publish ready collision replacements at a main-thread boundary before physics stepping. Publish render snapshots after the selected camera transforms are current and before command generation. Audit and adjust the current render-prep ordering explicitly, with regression coverage for ordinary meshes and cameras.
- Pin pages used by active draws, colliders, edits, undo dependencies, and saves. Evict eligible pages by priority/recency with unload hysteresis. Remove command references before release and respect BGFX's deferred execution and buffer-memory ownership rules.
- Keep a bounded coarse coverage representation for the configured visible horizon while detailed blocks arrive. For datasets where a global overview is too large, page coarser hierarchy blocks too. Keep the selected coverage balanced across resident boundaries; unavailable neighbor detail may require holding adjacent patches at a coarser level. Missing or corrupt detail keeps valid coarse coverage and records a failure with controlled retry; it never starts a per-frame retry loop.
- On a teleport or initial spawn, use an explicit readiness/loading barrier for required collision. Do not start simulation over absent ground. Normal movement prefetches beyond the physics safety region; falling back to coarse visuals does not make collision ready.
- Provide a deterministic drain mode for tests/captures, following the engine's existing deterministic-loading convention. Normal play never waits for all terrain loads.
- On scene removal, asset replacement, Play/Stop, or shutdown, invalidate request generations, drop instance pins, retire render/physics handles, and join or drain owned work before destroying dependencies. Recreate GPU representations on device restoration from surviving data or reloadable pages.

## Collision, navigation, and materials

**Collision:** start with static triangle meshes at canonical sample resolution through a terrain adapter to `PhysicsCore`, independent of camera LOD. Any later collision downsampling needs an explicit physical error/spacing setting and a documented tolerance against canonical height queries. Bound its resident region around players, characters, and relevant dynamic bodies; a spectator camera must not remove the ground under gameplay. Include border neighbors and verify contact stability across tile seams. Cache by asset/tile/revision rather than an incidental render `MeshData*`. Build replacement CPU data asynchronously; keep the previous shape until a safe main-thread swap. After an edit, track pending collision revisions and require readiness before entering play.

**Navigation:** add a terrain triangle provider to `NavigationCore::GatherInput`, respecting collection bounds, layers, and modifiers. Bake from a pinned, revisioned source/cooked snapshot, not whichever visual patches happen to be resident or whichever LOD a camera chose. Large offline bakes can read intersecting pages in batches without loading the whole terrain into GPU memory. Terrain edits mark affected navigation tiles stale; later rebakes and streamed Detour tiles are a separate milestone. Runtime obstacle carving does not by itself implement terrain/navmesh streaming.

**Materials:** use `StandardMaterial` for the first terrain. Later add a `TerrainMaterial` with four paintable layers, tiled base color, normal, and packed ORM, with normalized weights and a defined default layer when weights sum to zero. Reserve stages 8–15 for existing lighting. Pack layer data into three texture arrays where supported, plus the weight map, with a padded atlas fallback; check actual backend limits before enabling that path. Define mip/gutter handling and material batch keys for per-tile bindings. Defer Shader Graph terrain nodes until this binding contract is stable.

## Editor workflow and asset safety

Use a `TerrainDocument` with its own `UndoStack`, following the engine's existing separate-document convention. Route Undo/Redo to the focused terrain tool while it owns a stroke, and to scene history for scene edits. The existing stack limits command count, so terrain history also needs a byte budget or compressed/spilled deltas.

The first workflow is **Create flat terrain / Import heightmap → place in scene → sculpt → save → reopen**. The inspector exposes asset dimensions, range, material, collision settings, and readiness. A terrain tool provides a brush ring, radius, strength, and falloff; begin with raise/lower, then smooth and flatten, then layer painting. Keep brush settings as user preferences.

- Acquire viewport input for the duration of a stroke so selection and transform gizmos cannot consume the same gesture. Handle release outside the viewport, focus loss, Escape, scene changes, and tool switching explicitly.
- Space brush stamps by distance along the stroke and define time-based behavior for a stationary brush. Smooth from a stable neighborhood snapshot, not an update-order-dependent in-place traversal. Flatten captures its target height at stroke start.
- One stroke records before/after sample and weight deltas across all touched tiles as one undo command. Identify the document by asset GUID and samples by integer coordinates. Never store component, mesh, or sample-buffer pointers in undo records.
- Load and pin all required source pages and border halos before modifying an affected region. An unloaded neighbor is not permission to omit half a border edit. Preview staged requests and allow cancellation while waiting.
- Track dirty rectangles and revisions. Update affected mesh/normal regions and conservative bounds at a throttled rate, then finish exact derived work on stroke completion. Stale worker results cannot overwrite newer samples. Expose collision/navmesh lag separately from visual preview.
- Scene dirty state and terrain asset dirty state are distinct. **Save All**, close prompts, autosave, and recovery must include both. Saving a scene reference alone cannot mark edited height pages saved.
- Stage changed source pages as a new generation, then atomically publish the manifest referencing that generation. Keep the previous generation until publication succeeds; a failed or interrupted save must leave a complete readable asset. Publish cooks similarly. Do not assume renaming several files independently is one atomic transaction.
- External reimport detects unsaved document conflicts. Play mode uses an isolated snapshot or immutable published generation; runtime changes never alter authoring buffers, and Stop restores edit-time state.

## Milestones and acceptance gates

Each milestone should remain usable on its own. Streaming can follow the LOD work while advanced authoring continues, but neither should bypass the data and lifecycle contracts.

| Milestone | Deliverable | Acceptance gate |
| --- | --- | --- |
| **1. Data foundation — implemented** | Pure heightfield library, addressing/borders, versioned source manifest/pages, simple cooker and height queries | Round trip a 2 × 2 terrain; negative coordinates and shared corners agree; invalid input fails cleanly; quantization error is bounded |
| **2. Resident terrain — implemented** | Registered `Terrain` and explicitly scheduled `TerrainCore`, generated patches, one lit material, scene references, static collision adapter | Terrain renders and picks as one entity in editor/game; character crosses tile borders; save/reopen and Play/Stop work; unload leaves no terrain commands or bodies |
| **3. Basic authoring — authoring/recovery implemented** | Terrain document, flat creation/RAW import, four sculpt brushes, cross-tile delta undo, shared previews, independent copies, safe asset saving, restart recovery and incremental previews. Companion operations remain a follow-up | One stroke across four tiles undoes/redoes exactly; reopen preserves borders; failed save preserves the previous generation; editing never creates scene patch entities |
| **4. Terrain LODs** | Error-based selection, neighbor balancing, stitch variants, hysteresis and LOD debug view | Slow camera sweeps and forced levels show no internal cracks; two cameras both retain requested detail; collision/queries stay stable as visual levels change |
| **5. Streaming** | Cooked block directory/coarse coverage, priority scheduler, independent residency budgets, incremental uploads, collision readiness and cancellation | Traverse and teleport across a dataset larger than cache budgets; memory plateaus; missing pages retain coverage; no absent gameplay collision or stale publication after unload |
| **6. Authoring and surface layers** | Smooth/flatten, four-layer material/painting, import settings, navigation source integration and stale-tile reporting | Layer edits and height edits save/undo across borders; low-weight normalization is defined; navigation bakes match terrain regardless of current visual residency |
| **7. Scale-driven renderer work** | Per-view selection, hierarchy/coarse patch merging, shared GPU grids/displacement or a measured alternative | Profiling demonstrates a benefit; forward/shadow/picking silhouettes agree; existing camera, material, and viewport checks remain green |

### First implementation slice

Milestone 1 implements the following without renderer or editor coupling:

1. Define validated terrain settings, signed tile/sample coordinates, floor division, and global sample ownership.
2. Add an in-memory tile set with canonical sample access, shared-border synchronization, normal generation, and dirty-region expansion.
3. Build deterministic patch vertices/triangles, local bounds, and coarse-level geometric errors from a captured revision.
4. Add versioned manifest/page serialization, a minimal cook/read round trip, and explicit height query statuses.
5. Cover the contracts with focused tests; then hook a tiny resident test terrain into rendering in milestone 2.

Suggested placement: pure data and builders under `Source/Terrain/`, the component under `Source/Components/Graphics/`, the runtime core under `Source/Cores/`, GPU support under `Modules/Moonlight/Source/Graphics/`, and the authoring tool/document under `Modules/Havana/Source/`. Keep GPU classes out of the pure data headers. Use the existing metadata/asset database and register the new asset type and browser behavior deliberately.

## Validation and measurements

Run the focused terrain tests for the CPU foundation. Add the later integration tests when the corresponding behavior is implemented.

- **Pure data:** round trips and malformed/truncated pages, signed boundaries and outer edges, four-way corner edits, normals from neighbor halos, quantization, query/triangle agreement, conservative LOD errors and bounds.
- **LOD geometry:** all edge masks, supported levels and corner combinations; consistent winding, no uncovered cells, cross-tile neighbor balancing, threshold hysteresis, perspective and orthographic cameras.
- **Lifecycle/streaming:** delayed/out-of-order completions, rapid request cancellation, repeated unload/reload and Play/Stop, edits during decode/upload, two shared instances, tiny budgets, corrupt/missing pages, save interruption, and collision replacement with live bodies.
- **Editor:** extend `.edscript` automation as needed for terrain strokes and page/save assertions. Exercise asset move/duplicate, shared versus unique assets, border sculpting, exact undo/redo, focus loss, dirty flags, and reopen. Deterministic screenshots cover seams, debug modes, and later painted layers.
- **Integration:** editor and standalone game builds, existing render/camera checks after scheduling changes, collision seam traversal, and navmesh bakes from both resident and nonresident terrain data. Verify Linux locally and Win64/macOS through available builds; do not claim unrun platform checks.

Record CPU request/decode/build/publish time, upload bytes and time, resident/pinned/pending/retiring memory by representation, draw/triangle counts per view, collision readiness, cache hit rate, canceled/stale completions, and brush response latency. Add OPTICK scopes and a small terrain debug panel.

Use three fixtures: a 2 × 2 tile correctness terrain with a diagonal ridge and a four-tile brush crossing; a medium terrain for LOD sweeps and multiple cameras; and a dataset larger than the configured cache for streaming and teleport tests. Fix viewport, camera path, terrain data, and build configuration when comparing captures. Set numerical performance targets after recording the resident baseline; the proposed dimensions are not measured performance guarantees.

## Decisions to revisit after the first slice

- Expected world extent, nearest viewing distance, elevation range, and acceptable collision/visual error; these determine sample density and useful LOD depth.
- Whether a heightfield covers the intended gameplay. If destructible caves are central, resolve that before committing authoring formats.
- The common-topology multi-camera policy versus per-view draw lists once secondary views become expensive.
- Whether measured draw submission, vertex memory, shading, collision construction, or I/O is the first scaling bottleneck. Choose hierarchy merging or GPU displacement from that evidence.
- Source-page compression and history spill policy, after real authoring data and stroke sizes are available.

Related current-system documentation: [rendering](../Docs/Rendering-Pipeline.md), [resources](../Docs/Resources-and-Assets.md), [threading](../Docs/Jobs-and-Events.md), [editor](../Docs/Editor-Havana.md), [physics](../Docs/Physics.md), [navigation](../Docs/Navigation.md), and [scene serialization](../Docs/Serialization-and-Scenes.md).
