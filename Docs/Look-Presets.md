# Look Presets

A project's look in one pick (LOOK-S0): **Realistic**, **Stylized-Lit**, **Toon**, **Flat/Unlit**, **Pixel-2D**, **PS1**, **N64**, **Retro-PC**, **Painterly** and **VHS**. The data is `Engine/Assets/Config/LookPresets.json`. A project's `Assets/Config/LookPresets.json` adds presets, or replaces one with the same `Name`. The code is `Source/Engine/LookPresets.h`.

## What a preset sets

| Key | What it does |
|-----|--------------|
| `Camera` | Partial `Camera` component JSON merged onto every camera in the scene: render scale and upscale filter, pixel perfect (reference resolution, pixels per unit, snap), and the per-camera pass switches (sun shadows, SSAO, volumetric fog) |
| `Post` | Partial `PostProcess` JSON merged onto the camera, which gets a PostProcess when it has none: tonemapper, exposure, AA, SSAO, bloom, edge outlines, fog, grade, LUT, vignette |
| `Material` | The material for meshes still on the stock `StandardMaterial`, applied on request. A graph material keeps each mesh's colour as its `Base Color`. Toon uses `Engine/Assets/ShaderGraphs/Stock/Toon.shadergraph` (Custom Lit, LOOK-S7) and Flat/Unlit uses `Engine/Assets/ShaderGraphs/Stock/FlatColor.shadergraph` |
| `Import` | Texture import defaults for new images (`Format`, `MIPs`). The pixel looks import uncompressed and without mips. Existing `.meta` files keep their settings |
| `Caps` / `Lite` | A project-wide cap layer (the HardwareProfiles `Caps` shape), combined with the hardware profile and the player's quality preset; the stricter cap wins. `Lite: true` adds the **Lite pipeline** for low-end PCs and handhelds: no SSAO, shadows (cascades or local), image-based lighting, volumetric fog, depth of field or temporal AA |

## Using it

- **Project Settings > Look** picks the project's preset (`ProjectSettings::Look`, saved under `Product.Look`). Its `Import` and `Caps` apply at once and at every startup (`Engine` loads the presets after `HardwareBudget::Init`).
  - **Apply to Open Scene** merges `Camera` and `Post` onto the scene's cameras. With **Replace Standard materials**, it also swaps the stock materials.
  - Applying a second preset overrides the first. Fields a preset doesn't name keep their values. Apply isn't undoable; it marks the scene dirty.
- **edscript:** `look-apply <Preset> [| materials]` applies to the open scene. `look-project <Preset>` sets the project look's import defaults and caps (an empty name clears it).

## The presets

| Preset | Camera | Post | Other |
|--------|--------|------|-------|
| Realistic | full resolution, all passes | ACES, temporal AA, SSAO, bloom | |
| Stylized-Lit | full resolution | AgX, FXAA, more bloom, saturation 1.25, the Warm LUT at 60% | |
| Toon | no SSAO | AgX at -0.5 EV, FXAA, ink edge outlines, no bloom | Toon graph material |
| Flat/Unlit | no shadows, SSAO or volumetric fog | no tonemapping or post effects | FlatColor graph, Lite |
| Pixel-2D | pixel perfect 320 × 180, 16 px/unit, snapped | no AA or bloom | uncompressed, no-mip imports |
| PS1 | render scale 0.33, Nearest | Reinhard, no AA, Vintage LUT; **vertex snap 120**, **affine textures**, **15-bit colour, full dither** | uncompressed, no-mip imports |
| N64 | render scale 0.4, Bilinear | FXAA, thick fog, saturation 0.8; **three-point filtering**, **15-bit colour, light dither** | |
| Retro-PC | pixel perfect 320 × 200 | no AA, contrast 1.2; **the 16 EGA colours, dithered** | uncompressed, no-mip imports |
| Painterly | full resolution | AgX, bloom, saturation 1.2; **Kuwahara radius 6** | |
| VHS | render scale 0.6, Bilinear | Reinhard, bloom, vignette; **the VHSGlitch post graph** | |

Presets that set no era effects turn them off (and clear post graphs), so applying looks over each other stays clean.

## Era effects (LOOK-S11)

`PostProcess` > Era, in `PostProcessSettings`:
- **On Standard materials** (`Assets/Shaders/Era.sh`, the `u_eraParams` lighting uniform, per camera):
  - **Vertex Snap**: projected vertices land on a grid this many virtual pixels tall (PS1 wobble).
  - **Affine Textures**: textures interpolate in screen space (PS1 warping). It's done without `noperspective`: the vertex writes uv × w and w, and the fragment divides them.
  - **Three-Point Filter**: three texels per sample (N64).
  - Shader-graph and sprite materials don't take these, and shadows don't snap.
- **On the image** (`Post/Era.frag`, the last post pass, at the render size and before any upscale so its pixels are the art's): a **Kuwahara** painterly filter (radius up to 6), quantizing to a **Palette** image (one row of up to 64 colours), and **Color Bits** per channel (5 = 15-bit), each with ordered 4 × 4 Bayer **Dither**.
  - The chain ends in `FrameBuffer::EraInput` when any of these are on.
  - Stock palettes are in `Engine/Assets/Textures/Palettes` (EGA, PICO8, Handheld4).

The era references are `Look_PS1`, `Look_N64`, `Look_RetroPC` (palette), `Look_Painterly` (Kuwahara) and `Look_VHS` (CRT/VHS). The base scene has a tiled stone floor and a textured crate, so the affine warp and the filtering show.

## Proof

- **Showcases:** `Assets/Scenes/Showcase/Look_<Name>.lvl`, one per preset (ten), each with a screenshot-regression reference. They are built from `Assets/Scenes/Tests/LookBase.lvl` by `Tools/BuildLookShowcases.edscript` (the editor's own `look-apply`). Rerun it after changing a preset, then `--update` the references.
- **Flows:** `LookFlows.edscript` applies Toon (outlines on, temporal AA off, the sphere's material swapped with its colour kept), then PS1 over it (render scale and filter, outlines off).
- **Unit tests:** `LookPresetsTests.cpp` covers parsing and replacing by name, the Lite caps, the eight engine presets, merging onto a camera (with a PostProcess added and untouched fields kept), and the import defaults.

## Caveats

- The project-wide parts (`Caps`, `Lite`, `Import`) follow the project's look, not the scene: the per-look showcases show the camera, post and material parts.
- New primitive meshes created in the editor still start on StandardMaterial; Apply swaps them.
