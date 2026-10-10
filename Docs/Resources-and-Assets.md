# Resources and Assets

All asset loading funnels through the `ResourceCache` singleton: a thread-safe, string-keyed map of `SharedPtr<Resource>` with synchronous loading plus **background loading** for textures, `.meta` JSON sidecars per asset (carrying asset GUIDs), and an editor/tools-only cook step (`Export()`) that compiles sources (`.png`, `.fbx`, `.vert`…) into runtime formats (`.dds`, `.assbin`, `.<platform>.bin`). Unreferenced resources are kept alive for a grace period, and tools builds hot reload assets when files change. This doc covers the load flow, async loads, the metadata/cook system, hot reload, the resource type inventory, and how to add a new type.

> Verified against engine commit dab803a2, 2026-10-08; model import (animation and skin data) against 07617c5f, 2026-10-09; pass-through assets against afce7083, 2026-10-09; async loading against 4d4de548, 2026-10-10.

## Overview

A `Resource` is anything constructed from a `Path` with a `Load()` override. `ResourceCache::Get<T>(path)` loads on the spot; components call it directly during deserialization (`AudioSource` sounds, prefab JSON…), so a cold `Get<ModelResource>` pays the full Assimp import on the calling (main) thread. `GetAsync<T>(path)` returns at once and loads types that support it on loader threads. Material textures, model textures, particle textures and asset-browser thumbnails load this way, so a scene's textures no longer block its first frame. A 2K texture cost about 30 ms of main-thread time before and costs under 0.1 ms now. Audio has its own streaming option (see `Docs/Audio.md`).

Cooking is driven by metadata, not by a build step: in `ME_TOOLS` builds, `Get` checks whether the asset's compiled twin exists / whether the source changed, and shells out to the appropriate compiler on the spot. Shipped (non-tools) builds only ever read the compiled files.

## Key Files

| Path | Role |
|------|------|
| `Modules/Dementia/Source/Resource/ResourceCache.h` | `Get<T>` template — the whole load/cook decision tree |
| `Modules/Dementia/Source/Resource/ResourceCache.cpp` | `Dump`, `TryToDestroy`, `LoadMetadata`, the async loader threads (`PumpAsyncLoads`, `WaitForAsyncLoads`, `CompleteLoad`), tools-only `FindByName` |
| `Modules/Dementia/Source/Resource/Resource.h` | Base class: `Load`, `Reload`, the async split (`SupportsAsyncLoad`, `LoadAsync`, `FinishAsyncLoad`), `IsLoading`, `HasLoadFailed`, `Metadata`, `FilePath` |
| `Modules/Dementia/Source/Resource/MetaFile.h` | `MetaBase` — sidecar contract: `Export()`, `GetExtension2()`, `FlaggedForExport` |
| `Modules/Dementia/Source/Resource/MetaRegistry.h` | Extension → metadata factory map, `ME_REGISTER_METADATA` |
| `Modules/Dementia/Source/Resource/AssetMetaCache.h` / `.cpp` | Timestamp cache backing the "was it modified" check |
| `Modules/Moonlight/Source/Graphics/Texture.h` / `Modules/Moonlight/Source/Graphics/Texture.cpp` | Texture resource + `texturec` cook |
| `Modules/Moonlight/Source/Graphics/ModelResource.h` / `Modules/Moonlight/Source/Graphics/ModelResource.cpp` | Assimp model resource + `assbin` cook |
| `Modules/Moonlight/Source/Graphics/ShaderFile.h` | Shader binary resource + `shaderc` cook (see `Docs/Materials-and-Shaders.md`) |
| `Source/Resources/JsonResource.h`, `Source/Resources/SoundResource.h` | Engine-side resource types |

## How It Works

### `Get<T>` decision tree

```mermaid
sequenceDiagram
    participant C as Caller (main thread)
    participant RC as ResourceCache
    participant MR as MetaRegistry
    participant Tool as External compiler (shaderc/texturec/assimp)
    C->>RC: Get<T>(path)
    RC->>RC: m_resourceStack.find(path.FullPath)
    alt cache hit
        RC-->>C: dynamic_pointer_cast<T> (null if T mismatches!)
    else miss
        RC->>MR: LoadMetadata(path) — factory by file extension
        MR-->>RC: MetaBase subclass (or null if extension unregistered)
        RC->>RC: compiledFileExists? (path + "." + GetExtension2())
        opt ME_TOOLS && (FlaggedForExport || !compiledFileExists)
            RC->>Tool: metaFile->Export() — shell out, blocking
            RC->>RC: metaFile->Save(), AssetMetaCache::Update
        end
        RC->>RC: construct T(path, args...), SetMetadata, T::Load()
        RC->>RC: m_resourceStack[FullPath] = res
        RC-->>C: SharedPtr<T>
    end
```

Details that matter:

- **Cache hits skip everything** — no metadata check, no export check, and the result is `dynamic_pointer_cast<T>`: calling `Get<Texture>` on a path already cached as another type returns **null silently**.
- The missing-file paths differ: source missing + compiled missing + not flagged → `YIKES("Failed to load resource: …")` and a null return; source missing but compiled present → logs `"Shit don't exist bro: …"` and **continues** (loads from the compiled file).
- The cache key is the raw `Path::FullPath` string — no normalization/dedup beyond what `Path` itself does.

### Async loads

`GetAsync<T>` runs the same decision tree up to the load. For a type whose `SupportsAsyncLoad()` is true, the resource is then cached and marked loading (`IsLoading()`), and it is queued:

1. A **loader thread** runs `LoadAsync()`, the CPU part (file IO and decoding; no GPU calls, no engine state). There are a few loader threads (a quarter of the cores, 1–4), separate from the job workers, so frame jobs never wait behind a file read.
2. The **main thread** runs `FinishAsyncLoad()` (the GPU upload). The engine calls `PumpAsyncLoads()` every frame before the late update and render, which finishes completed loads for up to 2 ms, always at least one.
3. The resource is then ready, or `HasLoadFailed()` (logged).

Rules:

- **`Get` always returns a loaded resource.** A `Get` of a path that is still loading finishes it on the spot: it runs the background part itself if no loader thread has started it, or waits for it. `CompleteLoad(resource)` does the same explicitly; hot reload uses it before `Reload()`.
- **Consumers must tolerate not-yet-loaded resources.** The renderer binds neutral fallbacks for a texture whose `TexHandle` isn't valid yet, and the asset browser shows the type icon until a thumbnail arrives.
- **`WaitForAsyncLoads()`** finishes everything and helps with queued loads (loading screens). Deterministic runs (`--frame-time`) call it every frame instead of pumping, so captures never depend on IO timing; the screenshot regression stays bit-exact.
- **`ReleaseAll()` cancels**: queued loads are dropped and marked failed, and the loader threads are joined. Engine shutdown does this before tearing the GPU down.
- **`GetPendingLoadCount()`** counts queued, running and not-yet-finished loads, for progress bars.
- To make a type async, override the three virtuals. `Texture` reads and parses the DDS in `LoadAsync` (`m_pendingImage`) and creates the bgfx texture in `FinishAsyncLoad`; its `Load()` is simply both. Types that don't opt in load synchronously through `Load()` even when requested with `GetAsync`.

### Metadata sidecars

`LoadMetadata` looks up the file **extension** in the `MetaRegistry` and constructs the registered `MetaBase` subclass. If `<asset>.meta` exists it's parsed and deserialized; in **editor builds** a missing `.meta` is created on the spot and the asset is flagged for export. `FlaggedForExport` is also set when `AssetMetaCache` (a JSON timestamp cache, loaded at `ResourceCache` construction and saved at destruction) sees a different `LastModified` than the sidecar — and it defaults to *modified* for assets it has never seen.

Registered metadata types:

| Extension | Metadata type | Compiled twin (`GetExtension2`) | Cook tool |
|-----------|---------------|--------------------------------|-----------|
| `png`, `jpg` | `TextureResourceMetadata` (+Jpg) | `dds` | `texturec` (`--as dds`, optional `-m` mips; BC1–BC7/ETC format options) |
| `fbx`, `obj` | `ModelResourceMetadata` (+Obj) | `assbin` | Assimp binary export (triangulated, smooth normals, tangents, at most 4 bone weights per vertex, left-handed); clips and skin weights survive into the `assbin` — see `Docs/Animation.md`. The prebuilt Linux Assimp only has the FBX, OBJ and Assbin importers. Texture references that don't exist (an artist's absolute or Windows path) are relinked by name: the same file next to the model or in a `Textures` folder, the same stem with a shipped image format, then the first image starting with the stem (`Texture_01.psd` → `Texture_01_A.png`); each relink is logged |
| `vert`, `frag` | `ShaderFileMetadata` / `FragShaderFileMetadata` | `<renderer>.bin` (e.g. `spirv.bin`, `dx11.bin`, `metal.bin`) | `shaderc` from `Tools/<platform>/` |
| `wav`, `mp3` | `AudioResourceMetadata` (+Mp3) — declared in `Source/Components/Audio/AudioSource.h` | — | none (pass-through: loaded by FMOD as is, see `Docs/Audio.md`) |
| `mat` | `MaterialResourceMetadata` (declared in `Modules/Moonlight/Source/Graphics/Material.h`) | `mat` | none (pass-through) |
| `inputactions` | `InputActionsMetadata` (`Source/Input/InputActionsAsset.h`) | `inputactions` | none (pass-through); a data asset, edited in the asset browser |

A metadata type can make its asset a **data asset** by returning a reflected object from `MetaBase::GetEditableType` / `GetEditableData`: the asset browser then draws it with the reflection inspector and writes it back through `SaveEditableData` (see `InputActionsMetadata`).

Extensions **not** in this table (e.g. `.json`, `.lvl`, `.html`) get **no metadata**: `LoadMetadata` returns null, no cook step runs, and `Get` goes straight to `T::Load()` on the source file.

### Lifetime: keep-alive eviction

`m_resourceStack` holds one owning `SharedPtr` per resource. `ResourceCache::Dump()` runs at the bottom of every frame but does its work at most four times a second: a resource whose only owner is the cache is remembered as unreferenced, and evicted once it has stayed unreferenced for `SetKeepAliveSeconds` (default 15 s). Anything referenced again before then is kept. `ReleaseAll()` drops every cache reference; `TryToDestroy(Resource*)` erases one refcount-1 entry immediately.

### Hot reload (tools builds)

`Engine::Run` starts a `FileWatcher` (`Modules/Dementia/Source/Resource/FileWatcher.h`) over `Assets/` and `Engine/Assets/` before `Game::OnStart`. It polls file modification times on a background thread (build artefacts like `.bin`, `.d`, `.dds`, `.assbin` are ignored) and `Engine::PollAssetChanges` consumes the changes at the start of each frame: `ResourceCache::OnFilesChanged` re-exports each affected cached resource from its metadata and calls `Resource::Reload()`. Editing a `.meta` reimports the asset it describes, and changed `.prefab` files clear the prefab cache. A resource type only reloads if it overrides `Reload()`.

### Asset GUIDs

`MetaBase` reads and writes a `"GUID"` (hex) in each `.meta`; one is generated the first time a meta is written. Prefabs carry their GUID inside (`"AssetGUID"`). `AssetDatabase` (`Modules/Dementia/Source/Resource/AssetDatabase.h`) maps GUID ↔ project path:

- It is filled by scanning `.meta` files and prefabs, at startup in tools builds and lazily (`EnsureScanned`) in game builds.
- It is updated whenever a meta is read or saved.
- It follows editor moves (`Move`), keeping the former paths (`FindMovedPath`). `ResourceCache::Get` of a path that moved this session loads from the new path.

Components store asset paths; saved scenes and prefabs record the GUIDs behind them and remap moved paths on load (`Docs/Serialization-and-Scenes.md`, "Asset references").

### Tools-only path resolution

`ResourceCache::FindByName(root, filename)` (`ME_TOOLS` only) does a **recursive directory walk** matching by bare filename — convenient for editor asset lookups, `O(entire asset tree)` per call, and returns the *first* match, so duplicate filenames in different folders are ambiguous.

## How to Extend

### Add a resource type

1. Subclass `Resource`:

```cpp
// Source/Resources/CurveResource.h
#pragma once
#include "Resource/Resource.h"

class CurveResource
    : public Resource
{
public:
    CurveResource( const Path& path ) : Resource( path ) {}
    virtual bool Load() override;    // parse FilePath (or its compiled twin); return success
private:
    // parsed data
};
```

2. (Optional — only if the type needs cooking or import settings) add a `MetaBase` subclass and register it against the extension:

```cpp
struct CurveMetadata : public MetaBase
{
    CurveMetadata( const Path& p ) : MetaBase( p ) {}
    virtual std::string GetExtension2() const override { return "curvebin"; }
    virtual void Export() override { /* write FilePath + ".curvebin" */ }
    virtual void OnSerialize( json& outJson ) override {}
    virtual void OnDeserialize( const json& inJson ) override {}
};
ME_REGISTER_METADATA( "curve", CurveMetadata );
```

3. Load it anywhere with `ResourceCache::GetInstance().Get<CurveResource>( Path( "Assets/Curves/Jump.curve" ) )` and **store the `SharedPtr`** on whatever owns it — otherwise it is evicted after the keep-alive period.
4. If the resource wraps GPU/API handles, release them in the destructor — that *is* reliably called on eviction (unlike cores; see `Docs/ECS.md`).

## Caveats & Fragility

- **Only textures load in the background.** Model import (Assimp), shader loads and cooking (`Export()`, a shell-out even under `GetAsync`) still block the frame. A model's own load is synchronous, but its textures are async.
- **The cache is thread safe, GPU resources aren't**: `Get` takes a recursive mutex (held during `Load`), but loading textures/shaders/models creates bgfx objects, so call `Get`/`GetAsync` from the main thread (see `Docs/Jobs-and-Events.md`); only `LoadAsync` runs elsewhere.
- **Type-mismatched cache hits return null** (logged as an error). Two systems loading the same path as different types is still a bug.
- **Keep-alive is time-based, not budget-based**: there is no memory cap.
- **Editor builds mutate the asset tree on read**: missing `.meta` files are created, sidecars are rewritten, exports are triggered — running the editor is not read-only with respect to `Assets/`.
- **Cook failures are soft**: `Export()` implementations shell out and don't propagate failure; a failed compile typically surfaces later as a missing/stale compiled twin (`YIKES` on next run) or a broken load.
- **`AssetMetaCache` treats unknown assets as modified** — a fresh checkout re-exports everything it touches on first editor run.
- **Path-string keying**: `Assets/Foo.png` vs an absolute path to the same file are distinct cache entries; consistency of how callers build `Path`s is load-bearing.
- **`FindByName` first-match semantics** with duplicate filenames across folders.

## Related Docs

- `Docs/Materials-and-Shaders.md` — the `shaderc` cook in full detail
- `Docs/Jobs-and-Events.md` — why `Get` must stay on the main thread
- `Docs/Serialization-and-Scenes.md` — components resolving asset paths during deserialize
- `Docs/State-of-the-Engine.md` — async-loading and cache-policy improvement notes
