# Cooking

The cooker turns a project's scenes into a **cooked tree**: everything those scenes need, in
runtime form only. It holds no sources, `.meta` files, build inputs or editor content, and runs
from any directory (or from a pak). The cooker is the SHIP-2 step of the build pipeline.

Code: `Source/Tools/Cook/` (`Cooker.h`, `CookDependencies.h`), with the runtime side in
`Modules/Dementia/Source/Resource/CookedAssets.h`.
Tests: `Tests/Source/CookTests.cpp`, plus the "Cooked build" step of `Tools/RunChecks.sh`.

## Running it

```bash
# Cook the Lighting showcase into .tmp/Cook (the scene is recorded through the game first).
Engine/Tools/Headless.sh ./.build/Editor_Debug/Havana --cook --scene Assets/Scenes/Showcase/Lighting.lvl --out .tmp/Cook

# Run the game on the cooked tree...
(cd .tmp/Cook && ../../.build/Game_linux_Debug/Game_EntryPoint_x64 --scene Assets/Scenes/Showcase/Lighting.lvl)
# ...or on a pak of it (--pak <file> also writes one), from a directory with nothing in it.
```

| Option | Meaning |
|---|---|
| `--scene <lvl>` (repeatable) | Scenes to cook. Without any: the project's `Build.Scenes` plus its `StartupScene` (Project Settings) |
| `--out <dir>` | The cooked tree (default `.tmp/Cook`). It is replaced each time. Only an earlier cook (it has a `CookManifest.json`) or an empty directory is ever cleared |
| `--pak <file.mpak>` | Also packs the tree into one archive (`Docs/VFS.md`); the game mounts it with `--pak` |
| `--platform linux\|windows\|macos` | Target platform (default: this one). Picks the shader profile |
| `--game <exe>` | The game build to record scenes with. Default: the newest tools game build (Debug or Release) next to this editor build (`.build/Game_<platform>_<config>/Game_EntryPoint_x64`, or the `Player` there for a C#-only project). A failed recording quotes the game's last output lines |
| `--no-record` | Skip recording: a static cook only |
| `--deps <file>` (repeatable) | Extra `--record-deps` recordings to ship (a playthrough, a test run) |
| `--record-frames <n>` | How long each recording plays (default 240 frames) |

`Havana --cook` exits non-zero when anything failed. It writes no editor UI, but the recording
starts the game, so run it under `Tools/Headless.sh` in automation.

The asset browser's **Build Assets** button (and its *Cook All Textures, Models & Shaders* menu
item) runs the cooker's in-place pass on the whole project instead. That pass cooks every texture,
model and shader stage whose cooked twin is missing or stale (`Cook::CookInPlace`). It replaces
the old `BuildAssets` / `-CompileAssets`, which only loaded textures and shaders.

## Where dependencies come from

1. **The static walk** (`Cook::CollectStaticDependencies`) reads each scene's JSON and every
   string in it that names an existing file. It recurses into the JSON assets among them
   (prefabs, other scenes, `.mat`, `.sequence`, shader-graph manifests) and into JSON embedded in
   strings (serialized script fields carry asset references).
   - Moved assets are remapped through the data's GUID table first (`RemapAssetReferences`).
   - A `.shadergraph` reference becomes its generated `<Graph>.sg/Material.json` and that
     manifest's program stages (`@Assets/...` names a project stage; a bare name sits next to the
     manifest).
   - Baked navmeshes named `<Scene>.<Entity>.navmesh` next to the scene come along.
   - Config files (`Assets/Config/`) are settings, not content, so they aren't walked. The
     project's `StartupScene` ships only when the cook takes its scenes from the project.
2. **Recorded runs** (`--record-deps`, `Docs/VFS.md`): unless `--no-record`, the cooker plays
   each scene through the game for `--record-frames` frames, recording every logical path the
   VFS served. This catches what no scene names, such as assets scripts load by string and
   engine defaults. A twin or `.meta` read (`X.png.dds`, `X.frag.spirv.bin`, `X.png.meta`)
   stands for its asset (`Cook::OwningAsset`). A source the runtime read raw ships as those
   exact bytes: an RmlUi document's `.png` stays a `.png`.
3. **Always**: `Assets/Config/ProjectSettings.json`, `Engine.cfg`, `HardwareProfiles.json`, the
   project's input actions, `Build.AlwaysInclude` (files or whole folders), and every engine
   shader stage (the renderer prefetches them all at startup). `Build.Exclude` prefixes drop
   anything they match.
4. **Model textures**: each model's materials are read from its cook and resolved the way the
   loader does (`ModelResource::ResolveTextureReference`: relative, absolute, nearby by name,
   then by file name under `Assets`). The answers go into the registry, so a cooked run never
   searches the filesystem for a near match.

## What ships

| Asset | Ships as |
|---|---|
| Textures (`png`, `jpg`, `jpeg`, `tga`) | `<path>.dds` |
| Models (`fbx`, `obj`) | `<path>.assbin` (+ `<path>.vertexcache` when there is one) |
| Shader stages (`vert`, `frag`) | `<path>.<profile>.bin` for the target: `spirv` on Linux (Vulkan is forced), `dx11` on Windows, `metal` on macOS |
| Sprite images a `.spriteatlas` packs (see below) | packed into `<atlas>.spriteatlas.png.dds` + `.spritesheet`; the sources stay home |
| Everything else (scenes, prefabs, sounds, fonts, navmeshes, UI documents, config, render textures: a `.rendertexture` is its own runtime form) | as is |
| Never | `.meta`, shader build inputs (`.sh`, `.var`, `.sc`, depfiles), scripts and projects (`.cs`, `.csproj`, `.sln`; scripts ship compiled next to the executable), editor documents (`.shadergraph`, `.edscript`, `.md`), `Assets/Havana/`, `Assets/Icons/`, `imgui.cfg`, `.tmp/` |

Before writing, the cooker cooks in place whatever is missing or stale: the same `Export` a tools
build runs on first load (`texturec`, the Assimp export), and `shaderc` for the target profile
(stale = older than its source, `.var` or any include in its depfile). The jobs are spread over
up to 8 threads. `dx11` binaries only compile on Windows (shaderc's D3D compiler), so cooking for
Windows elsewhere fails unless they are already built.

Files are read through the VFS, so the game-over-engine overlay bakes in: the tree has one
`Assets/` with both. Next to it the cooker writes:

- **`CookManifest.json`**: the scenes, and every shipped file with its size and the asset it
  serves.
- **`AssetRegistry.json`** (format in `CookedAssets.h`): per asset, whether its source was
  stripped, its `.meta` JSON, its GUID and (models) its texture answers, plus the engine shader
  stages that shipped.

## Sprite atlases (ME-277)

A `.spriteatlas` lists sprite images to pack together so they batch:

```json
{ "Sources": [ "Assets/Textures/Sprites/Crate.png", "Assets/Textures/Sprites/Props/", "Assets/Textures/Sprites/Coins.spritesheet" ],
  "Padding": 2, "MaxSize": 2048, "Extrude": 1 }
```

Sources are images, folders of images (not recursive), or sprite sheets. A sheet adds each of its sprites, named `<sheet>/<sprite>`, with its pivot and borders; an image's sprite is named after the file. `Moonlight::SpriteAtlas` packs them with MaxRects (best short side fit, no rotation, deterministic order), with `Extrude` edge texels repeated outwards so filtering doesn't bleed. It writes a power-of-two RGBA image.

**At cook** (`PackSpriteAtlases`, after models expand):

1. **Find the sprites:** the cooker reads the cooked JSON assets for SpriteRenderers: their `Texture` (unless normal-mapped) and their `Sheet`.
2. **Pack and ship:** every atlas whose sources those sprites use is packed into `<atlas>.spriteatlas.png` and `.spriteatlas.spritesheet` beside it. These are generated and gitignored, like a texture's `.dds`. The atlas ships as a texture plus its sheet.
3. **Map:** `AssetRegistry.json`'s `SpriteAtlases` maps each packed image (`"<path>"`) and sheet sprite (`"<sheet>|<sprite>"`) to `{ Sheet, Sprite }`.
4. **Drop sources:** packed source images are left out of the cook unless a non-sprite field of a cooked JSON asset names them.

**In a cooked run**, `SpriteRenderer::Resolve` looks itself up (`CookedAssets::FindAtlasSprite`) and draws from the atlas:

- **Image sprites** keep their authored pivot, 9-slice and region, offset into the atlas.
- **Sheet sprites** take the atlas sheet's copy.

Source trees never see the map, so editing keeps using the sources.

**Proof:** cooking `Physics2D.lvl` with `Assets/Textures/Sprites/Physics2D.spriteatlas` packs 10 sprites into one 1024 × 128 atlas, and the cook carries no sprite `.png`.
- **Captures:** the cooked tree and its pak both match the Physics2D reference (0.19%).
- **Batches:** sprite batches go from 8 to 3 (`SpriteBatches` in `--perf-report`). The three that remain are render-state splits that a shared texture can't merge: the unlit sky, the lit sprites, and the unlit point-filtered coins.
- **Unit tests:** packing (no overlaps, in bounds, deterministic), composing (copies, extrusion, pivots and borders) and the registry map are covered.

**Limits:**
- RmlUi's `<sprite>` element and C# code that load a sheet's image directly still need that image. List it in `Build.AlwaysInclude`, or keep it out of atlases.
- **In the editor** (ME-282): the `.spriteatlas` inspector edits the sources, padding, max size and extrusion. **Pack** writes the atlas as a cook would, then previews it with every sprite outlined (hover for its name and size) and shows the fill. An existing pack previews on open. The Game view's stats overlay shows `Sprite batches`. edscript `atlas-pack <atlas> | <expected count>` runs the same Pack (`SpriteAtlasFlows.edscript`).

## Cooked runs

`Engine::Init` loads `AssetRegistry.json` from the overlay roots when there is one
(`CookedAssets::Load`, after `--pak` mounts). From then on:

- **Stripped sources still exist.** A stat-only mount below every root answers
  `Path( "X.png" ).Exists` but serves no bytes, so loaders go on to read the shipped twin.
- **Import settings come from the registry**: `ResourceCache::LoadMetadata` falls back to the
  folded-in `.meta` JSON, for the settings loaders still read (model LODs, audio streaming, ...).
- **GUIDs come from the registry**: `AssetDatabase::EnsureScanned` registers them instead of
  scanning `.meta` files, so moved-asset remaps in scenes still resolve.
- **Nothing exports**: `ExportIfNeeded` is a no-op (there's nothing to cook from).
- **The renderer prefetches the registry's engine shader stages** instead of scanning the
  shader source folder.
- **Model textures resolve from the recorded answers only.**

Scripts load from the executable's directory as always. The script build copies
`ScriptCore.runtimeconfig.json` there too, so a cooked tree runs scripts without the source tree.

**Proof**: the "Cooked build" step of `RunChecks.sh` cooks the Lighting, Playground and
RenderTexture showcases into one tree and one pak. It runs the game on the tree (working directory
`.tmp/Cook`), then from an empty directory with only `--pak`, and both must match the showcase
references. RenderTexture's camera feeds render into `.rendertexture` assets that ship as data;
its monitors are also checked region by region
(`../Assets/Scenes/Showcase/Reference/RenderTexture.regions.json`). On Linux (2026-10-25) the
Lighting and Playground cooked captures were byte-identical to the source-tree ones. Lighting
cooks to 82 files (71 MiB: five textures, 70 engine shaders, config, a font, the ICU data) with
no `.png`, `.fbx` or `.meta` among them.

## Not yet

- **Terrain**: stream archives and texture caches still read physical files, so terrain scenes
  don't cook yet.
- **Compression and binary scenes**: pak entries are stored uncompressed, and scenes stay JSON
  (FND-17 turns them into CBOR).
- **Shipping**: Build Game (`Building.md`) puts a Retail executable, the scripts, an app-local
  .NET and a launcher around a cook.
