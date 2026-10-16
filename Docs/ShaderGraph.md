# Shader Graph

Node-based shaders, edited in Havana and compiled to ordinary bgfx shader sources. A graph is a `.shadergraph` JSON file. Saving it (or editing it with **Live** on) writes the generated shaders and a manifest next to it in `<Name>.sg/`. Materials, particle systems, decals and post-process stacks reference the graph by path.

- **Targets:** what a graph renders.
  - **Lit** and **Unlit** surfaces, for meshes through `ShaderGraphMaterial`.
  - **Particle** colour, through `ParticleSystem.Graph`.
  - **Decal** surfaces, through `Decal.Graph`.
  - **Post Process** passes, through `PostProcess.Graphs`.
- **Compiler** (`Source/ShaderGraph/`): typed and stage-aware, with errors attributed to nodes. It emits readable bgfx-dialect code that compiles for SPIR-V, GLSL and Metal (and D3D on Windows).
- **Editor** (`Modules/Havana/Source/Widgets/ShaderGraph/`): dockable document windows.
  - A canvas with live previews on every node.
  - A blackboard of properties, an inspector, and a main preview.
  - Per-graph undo, and generated code in view.
- **Runtime:** `ShaderGraphAsset` loads a manifest and its variant programs, and reloads in place when the graph is recompiled.

> Verified against engine commit 71a95192 and later, 2026-10-16 (shader graph rebuild).

## Overview

| Concept | Type | Notes |
|---------|------|-------|
| Graph | `ShaderGraph::Graph` (`Graph.h`) | Nodes (stable ids, settings, inline input values), links (by pin name), blackboard `Property`s, `TargetSettings`. JSON with a `Version` |
| Node type | `NodeDef` (`NodeDef.h`) | Pins (fixed or dynamic-vector groups), settings, stage mask, target mask, `Emit()`. Most are `ExpressionNode` templates |
| Target | `TargetDef` (`Targets.h`) | Output pins, which builtins exist per stage (and how they're computed), templates, texture budget, programs |
| Compile | `ShaderGraph::Compile` (`Compiler.h`) | Graph → `CompileResult`: diagnostics, resolved pin types, generated files, manifest; `Preview` mode builds the editor's preview program |
| Export | `WriteGeneratedFiles` / `CompileAndExport` | Writes `<Name>.sg/` (only files that changed, dropping their stale binaries) |
| Runtime asset | `Moonlight::ShaderGraphAsset` | `Material.json` + programs (`Forward`, `Skinned`, `Shadow`, `ShadowSkinned`); packs and binds parameters |
| Material | `ShaderGraphMaterial` | Per-mesh parameter / texture overrides for a Lit or Unlit graph |

## Key Files

| Path | Role |
|------|------|
| `Source/ShaderGraph/ValueType.h` / `.cpp` | Types, conversions (`ConvertExpression`), literals that are valid in every dialect |
| `Source/ShaderGraph/Graph.h` / `.cpp` | The graph model and its JSON |
| `Source/ShaderGraph/NodeDef.h`, `NodeRegistry.cpp` | Node definitions, `ExpressionNode` templates (`{A}`, `{=A}`, `{@T}`, `{B:Time}`, `{L:x}`, `{S}`), builtins |
| `Source/ShaderGraph/Nodes/*.cpp` | The node library by category |
| `Source/ShaderGraph/Targets.h` / `.cpp` | Lit, Unlit, Particle, Decal and Post Process targets; preview builtins |
| `Source/ShaderGraph/Compiler.h` / `.cpp` | Compilation, preview generation, export, shaderc error mapping |
| `Assets/Shaders/ShaderGraph/SGLib.sh` | Functions the nodes call (noise, voronoi, shapes, normals, blends, triplanar...) |
| `Assets/Shaders/ShaderGraph/SGPreview.sh` | Preview display and stand-in lighting / scene inputs |
| `Assets/Shaders/ShaderGraph/Templates/` | `Mesh.vert.sgt`, `Lit/Unlit/Shadow.frag.sgt`, `Particle.frag.sgt`, `Decal.frag.sgt`, `PostProcess.frag.sgt`, `Preview.*`, `Mesh.var` |
| `Assets/Shaders/StandardSurface.sh` | `shadeStandardSurface`: the forward shading Standard.frag and Lit graphs share |
| `Assets/Shaders/DecalCommon.sh` | `decalGeometry` / `shadeDecal`: shared by Decal.frag and Decal graphs |
| `Modules/Moonlight/Source/Materials/ShaderGraphAsset.h` / `.cpp` | Manifest, programs, parameter packing and binding |
| `Modules/Moonlight/Source/Materials/ShaderGraphMaterial.h` / `.cpp` | The mesh material and its inspector |
| `Modules/Moonlight/Source/Graphics/ShaderCompiler.h` / `.cpp` | shaderc wrapper (hot reload, previews, tests) |
| `Modules/Havana/Source/Widgets/ShaderGraph/ShaderGraphDocument.*` | One open graph: canvas, panels, undo, live compile/export |
| `Modules/Havana/Source/Widgets/ShaderGraph/ShaderGraphPreview.*` | Node-preview atlas and main preview (async shaderc) |
| `Modules/Havana/Source/Widgets/ShaderGraph/ShaderGraphWidget.*` | Open documents, action routing |
| `Modules/Havana/Source/Widgets/ShaderGraph/ShaderGraphAutomation.*` | `sg-*` editor automation commands |
| `Tests/Source/ShaderGraphTests.cpp` | Compiler tests; every node × target through shaderc; project graphs vs their generated files |

## Using it

- **Create:** in the asset browser, right-click > Create > Shader Graph > Lit / Unlit / Particle / Decal / Post Process.
- **Open:** double-click a `.shadergraph`, or click **Open in Shader Graph** on a graph material.
- **Edit:**
  - **Space** or right-click on the canvas opens the node palette. Its search covers names, keywords and categories, and lists blackboard properties too.
  - Drag from a pin into empty space for a palette of compatible nodes; the new node connects itself.
  - Incompatible links and loops are refused while you drag, with the reason.
  - **F** frames the selection (or everything). Right-drag pans.
  - Select a node to edit all its settings and inputs in the side panel.
  - Unconnected inputs edit inline on the node. Enum settings are buttons on the node.
  - Node menu (right-click a node): Copy, Duplicate, Convert to Property (constants), Show/Hide Preview, Group Selection, Delete.
  - Link menu: Insert Reroute, Delete.
- **Blackboard:** typed properties (Float, Vector 2/3/4, Color, Boolean, Texture 2D, Cubemap).
  - Each has a default, an optional slider range, Color/HDR flags, and **Exposed** (shown on materials, settable per material).
  - Drag a property onto the canvas, or pick it in the palette.
- **Actions:** while a graph window has focus, Ctrl+S, Ctrl+Z / Ctrl+Y, Ctrl+C / Ctrl+V / Ctrl+D, Delete and Ctrl+A act on the graph, through `EditorActions::DocumentClaims` / `DocumentExecute`. Each graph has its own undo history; a value drag is one step.
- **Live** (on by default): every successful compile is exported after a short pause. Hot reload then recompiles the scene's shaders, so materials update as you edit, unsaved. Closing without saving restores the saved graph's shaders.
- **Messages:** compiler errors and warnings, plus shaderc errors from the preview program (mapped to nodes through their `n<id>_` variables). Click one to focus its node. Nodes with errors get a red outline and a tooltip.
- **Show Code:** the generated sources and `Material.json`.

### Using a graph

| Target | Where | Notes |
|--------|-------|-------|
| Lit / Unlit | Mesh > **Use Shader Graph...** (or Material Type: Shader Graph) | `ShaderGraphMaterial`: exposed properties appear in the inspector; per-mesh overrides serialize as `Params` / `Textures` |
| Particle | `ParticleSystem.Graph` | Replaces the particle fragment shader; the system's Texture is the **Particle Texture** node |
| Decal | `Decal.Graph` | Replaces the decal's textures and surface values; angle fade still applies |
| Post Process | `PostProcess.Graphs` (a list, applied in order) | **Injection**: Before Tonemap (HDR, bloom and exposure see it) or After Tonemap (display image) |

Particle, decal and post graphs use their graph's defaults (no per-instance overrides yet).

## How It Works

### Compilation

`Compile( graph, options )`:

1. **Nodes and pins.** `NodeDef::BuildPins` runs per node: the output node's pins come from the target; Custom Function, Property and Swizzle pins depend on settings. Unknown types and target-incompatible nodes are errors.
2. **Links** resolve by pin name. Links to missing pins are dropped with a warning.
3. **Order:** a depth-first sort. A back edge (a loop) is reported on its node and cut.
4. **Types:** in dependency order. Pins in a dynamic group resolve to the widest vector connected to any of them, or to the group's declared width when none is. Each connection is checked with `CanConvert`, and truncation warns.
5. **Slots:** properties get parameter slots (`u_sgParams[0..15]`, one vec4 each) and texture slots (`s_sgTex0..`, up to the target's `MaxTextures`).
6. **Emit**, per generated file and stage:
   - The nodes that reach that file's output pins are emitted in order (CSE folds identical nodes).
   - Each output becomes `n<id>_<Pin>`, or an alias for cheap atomic reads.
   - The output node's inputs become `sgOut_<Pin>`.
   - Only the builtins actually used are declared, with their dependencies, as `sg_<Name>` locals.
   - A fragment-only node (derivatives, Screen Position, Triplanar...) feeding a vertex output is an error on that node. Vertex-stage texture samples switch to explicit LOD.
7. **Templates:** `{{DEFINES}}`, `{{DECLARATIONS}}`, `{{LOCALS}}`, `{{BODY}}` and per-file tokens (`{{SKIN_INPUTS}}`) are filled in. `TargetDef::GetFiles` decides the files and the manifest's programs.

**Dialect rules** (checked by the every-node test on all profiles):
- Float literals always have a point or an exponent.
- Floats splat with `vecN_splat`, and scalars are never swizzled.
- Multi-argument helpers take exact types: the HLSL path behind SPIR-V and Metal has no mixed-width overloads.
- `mul`, `texture2DLod`, `mtxFromCols` as in the engine's shaders.
- Derivative-using `SGLib` functions exist only in fragment shaders (`BGFX_SHADER_TYPE_FRAGMENT`).

### Generated files and programs

| Target | Files in `<Name>.sg/` | Programs (manifest) |
|--------|----------------------|---------------------|
| Lit / Unlit | `Forward.vert/.frag/.var`, `Skinned.vert/.var`; `Shadow.frag/.var` when the graph offsets vertices or clips (and casts shadows) | `Forward`, `Skinned`, `Shadow`, `ShadowSkinned` |
| Particle | `Particle.frag/.var` | `Forward` = engine `Particle.vert` + graph |
| Decal | `Decal.frag/.var` | `Forward` = engine `Decal.vert` + graph |
| Post Process | `Post.frag/.var` | `Forward` = engine `Post/Fullscreen.vert` + graph |

The `.var` files are copies of the engine's, so the stages always agree.

**Varying order.** bgfx's SPIR-V path gives varyings locations in name order over what each stage declares. A fragment `$input` must therefore list exactly its vertex shader's `$output`, unused ones included; otherwise values scramble silently. A test checks every generated pair.

Generated sources are committed with the graph. Game builds cook them like any shader (a rewritten source loses its stale binaries). Run the tests with `ME_SHADERGRAPH_UPDATE=1` to regenerate every project graph; without it the tests fail when a graph and its generated files disagree.

### Runtime binding

**Uniforms.**
- All graph programs share `u_sgParams[16]` and the samplers `s_sgTex0..7` on stages 0-7. bgfx uniforms are global by name, so the layout is fixed and nothing is created per material.
- Lighting keeps stages 8-15 (`BindLighting`).
- Every declared sampler is always bound: the assigned texture, else the property's fallback (`Moonlight::GetDefaultTexture`: white, black, gray, flat normal, black cube).
- Colour properties are authored in sRGB and packed as linear.

**Meshes.**
- `ShaderGraphMaterial::PrepareForDraw` (called by `RenderCore`'s gather job) follows recompiles.
- `Use()` binds the packed block.
- It is instanced like `StandardMaterial`; the batch key includes the parameter block.
- The shadow pass uses the graph's `Shadow` program via `Material::GetShadowProgram` (also for skinned casters), so offsets and clipping cast correct shadows.

**Particles / decals.** The renderer binds the graph's block and textures. Extra samplers:
- particles: `s_sgParticleTexture` (stage 6) and `s_sgSceneDepth` (7);
- decals: `s_sgSceneDepth` (7).

**Post.**
- `RenderGraphPostPasses` (`Renderer.cpp`) runs Before Tonemap graphs after TAA, ping-ponging `FrameBuffer::GraphHdr` and replacing `PostInput`.
- With After Tonemap graphs, `PostProcess::Render` writes `GraphLdr[0]`, and the last graph writes the camera's output.
- Passes get `s_sgSceneColor` (6), `s_sgSceneDepth` (7), and the camera's projection terms and inverse view (full-screen views are orthographic).

### Targets and builtins

| Builtin (`sg_*`) | Lit/Unlit vertex | Lit/Unlit fragment | Particle | Decal | Post |
|------------------|:---:|:---:|:---:|:---:|:---:|
| UV0 | ✓ | ✓ | flipbook cell UV | decal UV | screen UV |
| Position (object / world / view) | ✓ | ✓ | world, view | box, world, view | view, world (from depth) |
| Normal / Tangent / Bitangent (world) | ✓ | ✓ | camera-facing | surface (from depth) | from depth |
| View Direction, Camera Position | ✓ | ✓ | ✓ | ✓ | ✓ |
| Screen UV / Pixel, Front Face | | ✓ | ✓ | ✓ | ✓ |
| Time, Delta Time | ✓ | ✓ | ✓ | ✓ | ✓ |
| Particle colour, quad UV, rotation, velocity | | | ✓ | | |
| Decal fade | | | | ✓ | |
| Scene Depth node | | | ✓ | ✓ | ✓ |
| Scene Color node | | | | | ✓ |

The Lit fragment output feeds `shadeStandardSurface`, so view-shading debug modes, the sun with CSM, clustered lights, IBL and fog all apply. Normals are tangent space unless Normal Space is World.

### Previews

The compiler's `Preview` mode emits one fragment program for the whole graph:
- **What it evaluates:** every node (connected or not), on a flat swatch or an analytic sphere; the sphere is chosen automatically for position/normal/view inputs.
- **What a draw shows:** `u_sgPreviewSelect.x` picks it, a node's first displayable output.
- **The main preview** (`-1`) is the graph's result under a fixed light rig (`sgPreviewLit`), or its colour for non-lit targets.
- **Constants:** inline values and constant nodes read `u_sgPreview[]`.

Each edit re-runs codegen (cheap):
- If the preview code is unchanged (only constants moved), the editor just uploads the new uniforms.
- Otherwise `ShaderGraphPreview` runs shaderc on a worker thread and keeps the last good previews until it lands.

Tiles render into a 2048² atlas (128 px per node) on the editor offscreen view band (`RenderView::EditorOffscreenFirst`, ordered before ImGui). Graphs using time animate.

## How to Extend

**Add a node.** Most nodes are a few lines in `Nodes/<Category>Nodes.cpp`:

```cpp
ExpressionNode& node = InRegistry.Expression( "Bias", "Bias", "Math/Range" );
node.Keywords = "bias offset";
node.Inputs = { Dyn( "In" ), Dyn( "Bias", 0.5f ) };
node.Outputs = { Dyn( "Out" ) };
node.Expressions = { { "Out", "{In} / ((1.0 / {Bias} - 2.0) * (1.0 - {In}) + 1.0)" } };
```

- Put anything bigger in `SGLib.sh` and call it.
- Mark fragment-only nodes `Stages = StageFragment`, and restrict targets with `Targets`.
- The every-node test compiles the new node in every target on SPIR-V, GLSL and Metal.

**Add a target.** Subclass `TargetDef` in `Targets.cpp`:
- output pins, a builtin table per stage, the files and programs, and `MaxTextures`;
- a template under `Templates/` that defines `sgOut_*` consumers (and `sgSceneDepth` / `sgSceneColor` if it offers them);
- a hook where the engine binds the graph.

## Caveats & Fragility

- **Budgets:** 16 parameter vec4s per graph. Textures: 8 for Lit/Unlit, 7 for decals, 6 for particles and post.
- **Picking** ignores vertex offsets.
- **Decal graph texture samples** use implicit derivatives, so mips can pop along depth edges (the built-in decal uses one-pixel footprints).
- **No per-instance overrides** for particle, decal and post graphs yet: they use the graph's defaults.
- **Dynamic inputs:** the inline value of an unconnected dynamic input is a full vec4. The editor stores scalar edits on every component, so widening keeps their meaning; values written by hand in the JSON as `[v, 0, 0, 0]` widen to `(v, 0, 0)`.
- **Unsaved edits on quit:** quitting the editor with an unsaved graph (and Live on) leaves the exported shaders ahead of the saved graph. The project-graph test reports it.
- **Not verified here:** D3D (`s_5_0`) only compiles on Windows, and Metal/D3D rendering hasn't been run.

## Related Docs

- `Docs/Materials-and-Shaders.md`: material types, the shader pipeline, hot reload.
- `Docs/Rendering-Pipeline.md`: passes, views, post processing.
- `Docs/Editor-Havana.md`: widgets, actions, automation.
