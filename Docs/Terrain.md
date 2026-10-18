# Terrain Data Foundation

> Verified against engine commit `5e92b9b5` plus the terrain data foundation change, 2026-10-18.

The `TerrainData` namespace provides a CPU heightfield library: signed sample addressing, resident source tiles, shared-border edits, normals and height queries, patch mesh generation, and source/cooked serialization. It has no scene component, render submission, streaming scheduler, or editor tool yet.

## Overview

A `Heightfield` owns validated `Settings` and a map of resident tiles. Each tile stores `(TileQuads + 1)²` Float32 elevations, including copies of shared edges. Integer sample coordinates describe one terrain-wide X/Z lattice; sample spacing converts that lattice to metres. Y is elevation in metres.

`Settings` defaults to 256 quads per storage tile, 64 quads per render patch, 1 m sample spacing, and one tile. Tile and patch sizes must be powers of two, with patch size at most tile size. Tile size is capped at 4096 quads and patch size at 1024 to bound individual allocations. Dataset tile coordinates are signed 32-bit; sample coordinates are signed 64-bit.

## Key Files

| Path | Role |
| --- | --- |
| `Source/Terrain/Heightfield.h` / `Source/Terrain/Heightfield.cpp` | Settings, coordinates, resident pages, revisions, dirty samples, edits and queries |
| `Source/Terrain/TerrainMesh.h` / `Source/Terrain/TerrainMesh.cpp` | CPU patch vertices, local bounds, full/coarse index lists, geometric errors |
| `Source/Terrain/TerrainSerialization.h` / `Source/Terrain/TerrainSerialization.cpp` | JSON manifest, binary source pages, independently checksummed cooked tile blocks |
| `Tests/Source/TerrainTests.cpp` | Addressing, shared borders, missing data, query/mesh agreement, serialization and malformed input |

## How It Works

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

Use the public headers without renderer or editor headers. Capture a `Heightfield` copy before launching worker work; mutations of the same instance are not thread safe. After building a patch, compare its revision with the current source revision before publishing derived results.

The focused test filter is `--test-case=Terrain:*` on `MitchEngine_Tests`. The source and cooked codecs work on buffers; callers supply disk I/O and publication behavior.

## Caveats & Fragility

- There is no asset-type registration, `.meta` integration, file publication, scene serialization adapter, GPU mesh, collider, or editor tool yet. Manifest GUIDs are data only until connected to the asset database.
- Patch builds require owner samples and normal halos to be resident. Canonical height queries describe source data, not a separately coarsened collision surface.
- Coarse patch topology has no neighbor stitching or runtime LOD selection yet. The full vertex array remains allocated at every level.
- Cooked blocks currently contain complete tiles at one encoding, without compressed or independently streamed detail levels. The decoder eagerly reads all tiles.
- Source editing stores heights only; layer weights, raycasts, brush operations, and authoring history are not implemented.
- Copies are deep CPU snapshots. Snapshot sharing and asynchronous lifetime management are not part of this library.

## Related Docs

- [Terrain system plan](../Plans/Terrain-System.md) — milestones beyond the data foundation
- [Rendering pipeline](Rendering-Pipeline.md) — the existing mesh command path
- [Resources and assets](Resources-and-Assets.md) — metadata and background loading
- [Jobs and events](Jobs-and-Events.md) — worker and main-thread boundaries
