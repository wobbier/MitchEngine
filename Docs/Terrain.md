# Terrain

> Verified against engine commit `fa3bf474` plus the terrain asset-operations change, 2026-10-18.

The terrain system loads a fully resident tiled heightfield into one `Terrain` scene component, renders internal patches through the ordinary lit mesh path, and provides static triangle-mesh collision at canonical sample resolution. The `TerrainData` library supplies signed addressing, shared-border edits, normals and queries, patch geometry, source/cooked serialization, brush strokes and source publication. Havana provides a terrain authoring panel with four sculpting tools, RAW heightmap exchange and recovery snapshots. Runtime LOD selection and streaming are not implemented yet.

## Overview

A `Heightfield` owns validated `Settings` and a map of resident tiles. Each tile stores `(TileQuads + 1)²` Float32 elevations, including copies of shared edges. Integer sample coordinates describe one terrain-wide X/Z lattice; sample spacing converts that lattice to metres. Y is elevation in metres.

`Settings` defaults to 256 quads per storage tile, 64 quads per render patch, 1 m sample spacing, and one tile. Tile and patch sizes must be powers of two, with patch size at most tile size. Tile size is capped at 4096 quads and patch size at 1024 to bound individual allocations. Dataset tile coordinates are signed 32-bit; sample coordinates are signed 64-bit.

## Key Files

| Path | Role |
| --- | --- |
| `Source/Terrain/Heightfield.h` / `Source/Terrain/Heightfield.cpp` | Settings, coordinates, resident pages, revisions, dirty samples, edits and queries |
| `Source/Terrain/TerrainMesh.h` / `Source/Terrain/TerrainMesh.cpp` | CPU patch vertices, local bounds, full/coarse index lists, geometric errors |
| `Source/Terrain/TerrainSerialization.h` / `Source/Terrain/TerrainSerialization.cpp` | JSON manifest, binary source pages, independently checksummed cooked tile blocks |
| `Source/Terrain/TerrainEditing.h` / `Source/Terrain/TerrainEditing.cpp` | Distance-spaced strokes, sample deltas and source triangle picking |
| `Source/Terrain/TerrainAsset.h` / `Source/Terrain/TerrainAsset.cpp` | Source loading and generation-based manifest publication |
| `Source/Terrain/TerrainHeightmap.h` / `Source/Terrain/TerrainHeightmap.cpp` | Explicit RAW Float32/UInt16 height exchange and atomic exports |
| `Modules/Havana/Source/Widgets/Terrain/TerrainWidget.h` / `Modules/Havana/Source/Widgets/Terrain/TerrainDocument.h` | Terrain panel, document history and shared previews |
| `Tests/Source/TerrainEditingTests.cpp` | Cross-tile strokes, source picking and publication failures |
| `Source/Terrain/TerrainRecovery.h` / `Source/Terrain/TerrainRecovery.cpp` | Bounded, checksummed, self-contained recovery records and atomic file publication |
| `Modules/Havana/Source/Widgets/Terrain/TerrainRecoveryWidget.cpp` | Timed snapshots, the recovery manager and restore/discard actions |
| `Source/Terrain/TerrainFileOperations.h` / `Source/Terrain/TerrainFileOperations.cpp` | Staged source transfers, independent generations and metadata identities |
| `Tests/Source/TerrainFileTests.cpp` | Independent copies, preserved identities and failed-transfer cleanup |
| `Tests/Source/TerrainRecoveryTests.cpp` | Recovery round trips, corruption, publication failures and external source conflicts |
| `Tests/Source/TerrainTests.cpp` | Addressing, shared borders, missing data, query/mesh agreement, serialization and malformed input |
| `Source/Terrain/ResidentTerrain.h` / `Source/Terrain/ResidentTerrain.cpp` | Immutable CPU snapshots, patch preparation, collision tiles and translation validation |
| `Source/Resources/TerrainResource.h` / `Source/Resources/TerrainResource.cpp` | Background source/cooked loading, metadata registration and snapshot publication |
| `Source/Components/Graphics/Terrain.h` / `Source/Components/Graphics/Terrain.cpp` | Reflected asset/material/collision settings and resource readiness |
| `Source/Cores/TerrainCore.h` / `Source/Cores/TerrainCore.cpp` | Engine-owned publication, supported-transform checks and collision readiness |
| `Source/Cores/Rendering/RenderCore.cpp` | Internal patch meshes, GPU ownership, shared visibility indexing and terrain selection IDs |
| `Source/Cores/PhysicsCore.cpp` | Static collision, shared immutable-source cache and replacement/teardown |
| `Tests/Source/TerrainRuntimeTests.cpp` | Resident resource publication, scene settings, collision, readiness and lifecycle tests |
| `Tools/BuildTerrainTest.py` | Regenerates the four-tile demo asset and the game repository's terrain test scene |

## How It Works

### Resident scene integration

Add a `Terrain` component to an entity with a `Transform` and choose a `.terrain` manifest or `.terrainc` cooked package. The asset browser recognizes both as Terrain assets. Reflected fields cover the asset reference, base color, roughness, shadow casting, collision, friction, and restitution. Scene and prefab serialization stores those settings, not height arrays or generated children.

`TerrainResource::LoadAsync` reads and validates the asset and source pages, then prepares `ResidentTerrain` CPU geometry on a resource loader thread. `FinishAsyncLoad` publishes one immutable snapshot. `ResourceCache` shares the resource between instances; render and physics adapters retain snapshot references independently. When a `.meta` GUID exists, it supplies the runtime asset identity; standalone loads use the GUID embedded in the manifest/package. Metadata is pass-through and does not invoke the texture/model importers.

`TerrainCore` is engine-owned and explicitly registered/scheduled by `Engine`. It validates the final world matrix, including parents: translation is supported, rotation and scaling are rejected, and terrain cannot belong to a dynamic/kinematic rigidbody. Errors appear in the component inspector. **Reload Terrain** retries a file load asynchronously, including a previously failed load.

At play start, required collision assets are completed before `PhysicsCore` starts. Each physics synchronization refreshes terrain readiness. If required terrain is missing, invalid, or failed, 3D physics stepping waits while its error remains inspectable; repairing the source and reloading it resumes physics. This is a resident-scene readiness barrier, not an asynchronous gameplay streaming system. Terrain with collision disabled does not impose that barrier.

`RenderCore` creates internal `MeshData` and mesh instances from the prepared patches, using `StandardMaterial` and the component's color/roughness. Patches share the owning terrain entity's selection ID and use the same visibility-index allocation as ordinary meshes/models. Each patch owns its material instance because material preparation runs in parallel jobs. Terrain bounds participate in editor selection bounds and Frame Selected. Render preparation now runs after resource finalization and camera late updates, so culling uses the current camera snapshot.

Terrain draws only full-resolution topology. Camera culling does not remove off-camera shadow casters. On replacement, disable, removal, scene unload, or Play/Stop, the render bridge removes command references before releasing GPU geometry. No patch becomes a scene entity.

The GPU picking pass uses the camera's visible ray segment rather than a fixed 100 m range. Framing a large terrain can therefore move the editor camera beyond 100 m while its rendered patches remain selectable.

`PhysicsCore` builds one static mesh shape per storage tile, with the same canonical triangle split as height queries. Cache entries retain immutable terrain snapshots and can serve multiple instances. Collision has no dependency on GPU mesh buffers or visual culling. Replacement collision meshes are prepared before live shapes are removed; inactive/removed instances release their shapes and unused cache entries. Entity layers and the project collision matrix apply to terrain queries/contacts.

### Demo and checks

`Tools/BuildTerrainTest.py` writes `../Assets/Terrain/ResidentDemo.terrain`, four source pages, and `../Assets/Scenes/Tests/TerrainTest.lvl` in the game repository. The scene contains one terrain, a camera, a directional light, and three physics boxes. `../Assets/Scenes/Tests/TerrainFlows.edscript` exercises selection, settings undo, save/reopen, and Play/Stop. Generated source pages use the documented version 1 format.

### Residency, borders, and editing

`LoadTile` validates sample count, finite elevations, and exact agreement with resident neighboring edge/corner copies before replacing any data. `CreateFlatTile` uses the same path. `UnloadTile` removes a tile; `SetPending` records an outstanding request for a nonresident tile without performing I/O.

`Settings::Owner` chooses the positive-side tile at a shared boundary and the last tile at the finite outer boundary. It uses `FloorDivide`, so negative positions follow the same rule as positive positions. `GetSample` reads the owner and reports `Ready`, `Pending`, `Unavailable`, or `OutsideBounds`; it does not fall back to a possibly stale shared copy.

`SetSample` requires every tile sharing the sample to be resident, then changes all copies together. It increments the heightfield revision once and marks intersecting tile-local `DirtySamples`, expanded by one sample for vertex normals. The same revision propagates to affected neighboring normal strips even when their heights do not change. Repeating an unchanged edit does not increment the revision. `ClearDirty` clears rectangles without changing content revisions.

### Queries and geometry

`Query` takes terrain-local X/Z in metres. It rejects non-finite inputs as `InvalidInput` and never loads missing data. Each quad uses triangles `(a, c, b)` and `(b, c, d)`, where `a/b/c/d` are its low-X/low-Z, high-X/low-Z, low-X/high-Z, and high-X/high-Z corners. Height interpolation and the returned geometric normal follow that split; a saddle therefore differs from bilinear interpolation.

`GetNormal` computes a smooth vertex normal using adjacent height samples. It uses central differences internally and one-sided differences at the finite outer boundary. Missing owner/halo data returns a query status rather than manufacturing a normal.

`BuildPatch` reads an immutable heightfield snapshot and creates patch-local positions, smooth normals, normalized UVs, bounds, and triangle index lists. `SampleOrigin` preserves the integer terrain location independently of Float32 local vertex positions. Levels use strides 1, 2, 4, … through the patch size over one full-resolution vertex array.

`PatchLod::MaxError` conservatively bounds vertical deviation from the fine triangle surface. The nested diagonals align, so the maximum of the piecewise linear difference occurs at a fine-grid vertex. Errors are rounded upward when conversion to Float32 would otherwise underestimate them. `PatchMesh::Revision` identifies the heightfield snapshot used to build it.

### Source formats

`SerializeManifest` / `DeserializeManifest` encode a version 1 JSON manifest with `Format: MitchTerrain`, a nonzero sixteen-digit hexadecimal `AssetGUID`, validated settings, and one page reference per declared tile. References use unique forward-slash relative paths; parent traversal and absolute paths are rejected. Serialization sorts pages by X then Z for deterministic output.

`SerializePage` / `DeserializePage` encode source pages explicitly in little endian:

| Field | Encoding |
| --- | --- |
| Magic / version | `MTHP`, UInt32 version 1 |
| Dimensions / coordinates | UInt32 tile quads, Int32 tile X and Z |
| Payload metadata | UInt64 byte length and FNV-1a checksum |
| Payload | Row-major IEEE-754 Float32 elevations |

Page readers validate settings, coordinates, exact length, checksum, and finite samples before publishing their output. The caller passes a decoded page to `LoadTile` to validate shared borders.

### Cooked format

`CookHeightfield` requires every source tile. It produces a version 1 `MTRN` package with an 88-byte header, a 40-byte directory record per tile, and independently checksummed payloads. Header and directory checksums cover their respective metadata. All integers and Float32 bit patterns are written explicitly in little endian, without raw structure serialization.

The header records settings, encoding, asset GUID, tile count, and a source hash. Source hashing includes the cooker version, encoding, settings, coordinates, and Float32 source samples. Each directory entry records coordinates, absolute byte offset, payload byte length, checksum, and decoded minimum/maximum elevation. Directory records and payloads are ordered by tile X then Z.

Float32 cooking preserves source height bits. UInt16 cooking uses the asset-wide `HeightBase` / `HeightRange`, rounds to the nearest encoded sample, and rejects out-of-range heights rather than clamping. `CookInfo::QuantizationErrorBound` includes half an encoding step plus a conservative allowance for Float32 decode rounding; Float32 cooking reports zero.

`ReadCookedHeightfield` checks header/directory integrity, dimensions, counts, ordered coordinates, offsets, lengths, tile checksums, finite heights, decoded bounds, and shared borders. It constructs a temporary heightfield and publishes it only when the entire package passes validation. Loaded cooked tiles start with clean dirty rectangles. All codec APIs preserve output arguments on failure and return an error string.

## How to Extend

Use the pure data headers without renderer or editor headers. Capture a `Heightfield` copy before launching worker work; mutations of the same instance are not thread safe. After building a patch, compare its revision with the current source revision before publishing derived results. `BuildResidentTerrain` prepares a complete immutable snapshot for `Terrain::SetData`, which also supports procedural CPU input without adding scene children.

The focused test filter is `--test-case=Terrain:*` on `MitchEngine_Tests`. The source and cooked codecs work on buffers; callers supply disk I/O and publication behavior.

## Caveats & Fragility

- Source publication, automatic cooking, and sculpting tools are not implemented. Source-page changes do not automatically trigger a manifest reload; use **Reload Terrain**. Companion-directory move/duplicate operations are not yet grouped as one authoring operation.
- Patch builds require owner samples and normal halos to be resident. Canonical height queries describe source data, not a separately coarsened collision surface.
- Coarse patch topology has no neighbor stitching or runtime LOD selection yet. Rendering uses the full vertex array and finest index list; `MeshData` also retains its ordinary CPU collision copies, independently of the terrain collision adapter.
- Cooked blocks currently contain complete tiles at one encoding, without compressed or independently streamed detail levels. The decoder eagerly reads all tiles.
- The material currently exposes solid base color and roughness. Layer weights, terrain raycasts, brush operations, terrain-specific authoring history, and C# terrain bindings are not implemented.
- Required terrain load failure holds 3D physics stepping; other gameplay/script updates continue. The resident CPU/GPU/collision data has no streaming or upload budget yet.
- Copies are deep CPU snapshots. Snapshot sharing and asynchronous lifetime management are not part of this library.

## Related Docs

- [Terrain system plan](../Plans/Terrain-System.md) — milestones beyond the data foundation
- [Rendering pipeline](Rendering-Pipeline.md) — the existing mesh command path
- [Resources and assets](Resources-and-Assets.md) — metadata and background loading
- [Jobs and events](Jobs-and-Events.md) — worker and main-thread boundaries

## Editor authoring

Open **View > Terrain**, use the command palette's **New Terrain...** / **Open Terrain...**, double-click a `.terrain` in the asset browser, or use **Edit Terrain** on a component. New Terrain creates a saved flat source asset with configurable tile counts, quads per tile, spacing and starting height. **Place in Scene** adds one undoable terrain instance and frames it. The creation dialog caps source data at one million stored samples; terrain rendering still uses resident data and full-resolution patches.

The panel has six workspaces: **Sculpt**, **Paint**, **Tiles**, **Foliage**, **Splines** and **Inspect**. Sculpt implements **Raise**, **Lower**, **Smooth** and **Flatten**. The Tiles workspace also implements RAW heightmap import/export. **Make Unique** in Asset & Build copies the active heights to a separate source asset and rebinds only the selected instance. Other tool buttons are disabled placeholders with explanatory tooltips, including material layers, erosion, tile streaming, vegetation, roads, LOD diagnostics and cooking. **Asset & Build** contains save/revert controls and the planned asset operations. Icons come from `Assets/Icons/outline/` and are decoded as SVGs into theme-tinted editor textures; the panel releases its textures before renderer shutdown.

Select a terrain instance, open its source document, then select a sculpt tool or enable the brush. Drag in World View to sculpt. The outer ring marks radius; the inner ring marks the fully weighted center. Shift swaps Raise and Lower for a new stroke. Radius, strength, falloff and spacing persist as editor preferences. Strength is metres per stamp for Raise/Lower and a clamped blend fraction for Smooth/Flatten. Stamps are spaced by `Radius * Spacing`; holding still adds no stamps. Flatten picks its target elevation at gesture start. Smooth gathers the whole stamp's results before changing samples so traversal order cannot change its neighborhood inputs.

`TerrainData::RaycastHeightfield` picks canonical source triangles using a quad DDA, independently of collision and rendering. `BrushStroke` requires all source pages, modifies shared samples through `Heightfield::SetSample`, and returns one deduplicated before/after delta array. A stroke can span four tiles. The tool owns the mouse gesture so picking, marquee selection and transform gizmos do not also consume it. Release outside the image completes a stroke; Escape or application focus loss cancels it. Switching tools/documents, navigation, selection changes and Play finish it. Painting is disabled during Play and for unsupported transforms.

Each `TerrainDocument` owns an `UndoStack`, a mutable CPU field and an immutable preview. Undo/Redo/Save route to the document while its panel or active brush viewport has focus; scene operations keep their separate history. The panel's undo/redo buttons always target its document. A stroke is limited to one million changed samples. History has a conservative 64 MiB delta budget in addition to the stack's command cap; reaching the byte budget releases older history before recording the latest stroke.

Preview publication creates an immutable resident snapshot, at most ten times per second during a stroke and once on completion. `Terrain::SetEditorPreview` shares it with every instance referencing that asset while preserving the serialized asset path. Play uses that immutable authoring snapshot; authoring is disabled until Stop restores the scene. `TerrainData::UpdateResidentTerrain` compares the current samples against the previous snapshot. It rebuilds patches intersecting each tile's changed sample bounds plus a one-sample normal halo, and collision tiles whose heights changed. Unchanged patches/collision arrays are shared through immutable pointers. The comparison does not depend on dirty rectangles, so saving before the next preview cannot hide changes. A new identity or changed settings triggers a full preparation.

`RenderCore` preserves mesh commands and GPU buffers for shared patches, retiring commands before replacing changed geometry. `PhysicsCore` caches cooked meshes by immutable collision tile, allowing old/new snapshots and multiple instances to share unchanged cooks. Instance shapes still rebuild together at the normal physics synchronization boundary; cooking an unchanged tile is avoided. The panel reports patch and collision-tile rebuild counts from the last CPU preparation. Full heightfield copying/comparison and changed geometry preparation still run on the editor thread; background authoring and source-page copy-on-write remain future work.

Brush Radius, Strength, Falloff and Spacing changes participate in the focused terrain's undo/redo history alongside strokes. A complete drag or committed numeric edit creates one command. Undo/redo updates the stored editor preference and the visible control. These commands return `false` from `UndoCommand::AffectsSavedState`, so adjusting or undoing a brush does not dirty terrain source heights, create recovery snapshots, or move the height save point. With no terrain open, the panel keeps a temporary tool history. Reopening/reverting a terrain resets its document history as before.

**Save Terrain** saves only the active asset; **Save All Terrains** saves all dirty terrain documents. **File > Save All** saves those documents and the scene. New/Open Scene and Quit check terrain dirtiness before the normal scene prompt. Closing the panel preserves its open documents; their unsaved state still participates in these prompts. **Revert from Disk** confirms discarding the active document. **Save Copy** creates a separate asset identity without rebinding scene instances; **Make Unique** also rebinds the selected instance, as an undoable scene edit. Other instances keep their original source and history. Terrain autosave/recovery is described below. Source asset operations in the browser are described below.

`TerrainData::SaveSource` writes all source pages to a fresh `<Name>.terrain-data/<generation>/` directory, then stages a neighboring manifest and atomically replaces the `.terrain` file. Windows uses `MoveFileExW`; macOS/Linux use same-directory rename. Serialization/write/publication failure removes the incomplete generation and leaves the previous manifest and pages intact. Successful saves retain previous generations; compaction is not implemented. The document checks the expected manifest text before staging and immediately before publishing to detect external manifest changes. This is an application-level publication guarantee, not a power-loss durability guarantee or a lock against concurrent external writers. Reopen validates every page and shared border before publishing a new preview.

Authoring code lives in `Modules/Havana/Source/Widgets/Terrain/`. CPU brush/picking and source file adapters live in `Source/Terrain/TerrainEditing.*` and `Source/Terrain/TerrainAsset.*`. `Tests/Source/TerrainEditingTests.cpp` covers cross-tile deltas, cancellation, event-independent spacing, stable smoothing, flattening, range limits, source picking, generation retention and failed/conflicting saves. The game repository's `../Assets/Scenes/Tests/TerrainAuthoringFlows.edscript` exercises document routing, shared instances, save/reopen, cancellation, panel workspaces, large fonts, real viewport mouse input and Play/Stop. `Tests/Source/TerrainHeightmapTests.cpp` covers heightmap dimensions, byte order, orientation, finite/range checks, quantization and file publication; `../Assets/Scenes/Tests/TerrainHeightmapFlows.edscript` covers import/export dialogs, RAW exchange, independent assets and undoing the instance rebind.


`Tests/Source/TerrainIncrementalTests.cpp` compares incremental output against complete builds for interior edits, normal halos, shared edges, four-way corners, outer boundaries and undo. `Tests/Source/TerrainRuntimeTests.cpp` checks shared collision cooking and retirement across simultaneous old/new instances. Editor builds also run `Tests/Source/EditorUndoTests.cpp` for tool-only save points, branches, pruning and transactions. The game fixture `../Assets/Scenes/Tests/TerrainBrushFlows.edscript` exercises a real multi-frame Radius drag, preference undo/redo, mixed stroke/settings history and incremental preview counts.

## RAW heightmap exchange

Use the toolbar's Import/Export icons, the Tiles workspace's **Heightmap** / **Export** buttons, or the **Import Terrain Heightmap...** / **Export Terrain Heightmap...** command-palette actions. Import creates a new terrain asset; it does not overwrite the current document. New Terrain's **Import RAW heightmap** checkbox switches between flat creation and file import. An absolute RAW source path can be entered directly; Browse uses the project's asset picker. Reimport recipes and resampling are not implemented.

`TerrainData::RawHeightmap` declares sample width/height, `HeightEncoding::UInt16` or `Float32`, byte order, Z-row orientation, physical offset and scale. Width and height must equal `TilesX * TileQuads + 1` and `TilesZ * TileQuads + 1`. All bytes must be present, with no extra header or trailing bytes. The first file column maps to minimum terrain X. The first row maps to minimum Z, or maximum Z with **Flip Z rows**. Signed source tile coordinates do not change file ordering.

UInt16 import computes `Offset + (raw / 65535) * Scale`; Float32 import computes `Offset + raw * Scale`. Export applies the inverse. The default UInt16 mapping uses the physical height range; Float32 defaults to metres (offset 0, scale 1). Nonfinite values, zero scale, mismatched dimensions, missing source pages, heights outside the configured terrain range, and unrepresentable export values fail explicitly. Source pages stay Float32, and shared borders decode from identical RAW samples. No texture/color import path is involved.

UInt16 export rounds to nearest, introducing up to `abs(Scale) / (2 * 65535)` metres of quantization error, plus Float32 decode rounding. Float32 preserves source samples exactly with the default metre mapping. RAW adapters cap individual files at 64 MiB; the editor's one-million-stored-sample creation cap also applies to imported assets. The export dialog accepts `.raw`, `.r16`, `.r32`, `.f32`, `.h16` or `.bin`; the encoding controls determine the bytes, not the extension. RAW files contain samples only, so record dimensions and format settings when exchanging them with other tools.


## Terrain autosave and recovery

Havana saves dirty terrain documents under the project's `.tmp/Autosave/Terrain/` directory. **Autosave terrain** in Asset & Build enables/disables automatic snapshots; the global `Autosave.IntervalSeconds` preference sets the interval (120 seconds by default, zero disables automatic saves). Snapshots continue while the panel is closed. **Save Recovery Now** and the **Save Terrain Recovery Now** command take a manual snapshot. `TerrainWidget::PrepareForPlay` finishes active strokes and captures enabled recovery snapshots before `EditorApp::Play` starts simulation. Timed snapshots skip an active gesture and retry after release; they never store a partially captured stroke. Autosave pauses during Play.

The **Recovery** button in Asset & Build and **Terrain Recovery...** in the command palette open the recovery manager. On startup, pending terrain snapshots prompt after the scene recovery prompt. The manager lists source paths, UTC timestamps and invalid-record errors. **Restore** opens the snapshot as dirty authoring data with a fresh undo history and publishes its immutable preview to matching instances. It leaves the saved source untouched. Restoring over an already dirty document is rejected; preserve that work with Save/Save Copy or revert it first. **Later** keeps snapshots for another session. **Discard...** confirms deleting only the chosen snapshot; open document edits remain available and can be backed up again.

`TerrainData::RecoveryInfo` retains the original asset path/GUID, save time and exact base manifest. A `.terrain-recovery` file combines bounded JSON metadata with a full Float32 cooked heightfield. Both sections have checksums. Metadata inspection reads only the header/metadata; restoring validates the complete payload, shared borders, identity and settings before changing the document. Version 1 limits metadata to 8 MiB and payloads to 128 MiB. File writes stage beside the destination and atomically replace the previous snapshot using the same platform publication policy as source/RAW saves. Snapshots are self-contained and remain usable if the original source pages are missing. Failed or corrupt restores retain both the snapshot and current authoring state.

Filenames include asset and editor-session identifiers, so separate sessions do not overwrite each other's snapshots. Saving/reverting a document or undoing it back to its save point removes its owned/consumed snapshots. Unresolved snapshots from earlier sessions remain until the user restores or discards them. Restored documents retain the original manifest as the source save precondition; `TerrainDocument::CheckSourceIdentity` also checks the `.meta` GUID. External source edits or identity changes reject Save while keeping the recovered data dirty. **Save Copy** rescues that data into a new independent source asset. Force-exiting the editor keeps dirty terrain recovery files.

Recovery publication and incremental preview preparation currently run on the editor thread; background authoring remains a follow-up. Recovery captures heights and source provenance, not the prior undo command history or editor selection. Scene recovery remains separate and restores scene references. Unattended runs do not scan/write the user's recovery directory; tests explicitly configure `.tmp/Tests/` storage. The paired `../Assets/Scenes/Tests/TerrainRecovery01Write.edscript` and `../Assets/Scenes/Tests/TerrainRecovery02Restore.edscript` run in separate editor processes and verify timed saves, cancellation, clean undo cleanup, Play/Stop, restore, source conflicts and saving a recovered copy. Run them together with `RunEditorFlows.py --only TerrainRecovery`.


## Terrain asset operations

The asset browser's **Duplicate**, **Rename**, drag-to-folder move, and **Delete** now handle `.terrain` data and metadata together. Duplicate copies the saved source with a fresh manifest/metadata GUID and an independent generation of height pages. It leaves unsaved authoring buffers alone; **Save Copy** and **Make Unique** use the current authoring heights. Browser rename/move preserves the effective `.meta` GUID (or the manifest GUID when no sidecar exists) and publishes all current source pages under the destination's `<Name>.terrain-data/<generation>/` directory. Custom metadata fields are retained. Invalid sources/metadata, occupied destinations/sidecars and write failures reject the operation.

`TerrainData::TransferSource` stages a complete source and metadata in a private directory beside the destination. It publishes the height generation, then metadata, then the manifest; move retires the original manifest and metadata only after destination publication. It rechecks source text/metadata before publishing and retiring. Failure rolls back files owned by this operation and leaves the original readable; if an I/O failure prevents rollback, it retains the complete destination and reports its location. This is a staged application operation, not a concurrent-writer lock or a multi-file crash/power-loss transaction. Transfers currently decode/rewrite the full saved heightfield on the editor thread.

Previous source generations remain at their existing locations because another manifest or retained history may reference them. This matches the source-saving retention policy; compaction remains future work. The browser hides `.terrain-data` directories and `.thp` page files from ordinary listings/searches so they are handled through their source asset. Folder moves keep their relative layout and notify open terrain documents. Case-only renames on a case-insensitive filesystem may require an intermediate name.

Open documents follow source/folder moves with their dirty heights, undo history and immutable previews intact. Scene instances update their paths and mark the scene dirty; saved scenes/prefabs follow the GUID through the asset database when next loaded. Undo that restores a former terrain path resolves to its current shared preview. Pending recovery records are updated to the new source path, and matching base manifests are updated to the destination publication. Existing external-save conflicts remain conflicts. Enabled recovery snapshots refresh after a move. If a recovery record cannot be migrated, the original is retained and the panel reports the error.

**Delete** uses the existing confirmation dialog and moves a terrain into a unique folder under `.tmp/Trash/`, including an independent saved generation and sidecar. The trashed terrain is readable without its old location. Assets/folders containing open dirty terrain documents are rejected until those heights are saved or reverted; the browser shows the reason inline. Clean matching documents close, previews/cache records retire, and scene references remain at their original path so deletion is visible. Old source generations retain the same history policy above. Asset file operations are not entries in the scene/terrain undo stacks; Trash is the retained copy.

`Tests/Source/TerrainFileTests.cpp` exercises identity/metadata preservation, independent pages, malformed sources and destination failures. The game fixtures `../Assets/Scenes/Tests/TerrainFileFlows.edscript` and `../Assets/Scenes/Tests/TerrainFileConflictFlows.edscript` run actual browser rename/duplicate/move operations, folder relocation, dirty history, recovery migration, GUID rescanning/remapping, former-path previews, Trash protection and preserved external conflicts. Unattended fixtures use unique `.tmp/Tests/` sources and never modify project assets.
