# Depth of field

> Verified against engine commit 7a92e84f plus the DoF working changes, 2026-10-21.

Add **PostProcess** to a perspective camera and enable **Depth of Field**. Existing scenes keep
DoF disabled. The editor scene camera inherits the main camera's post settings, as with other
effects; its own position and FOV still determine its focus and blur.

| Inspector control | Meaning |
| --- | --- |
| Focus Distance (m) | Camera-forward world distance to the sharp plane; drag at 1 mm per pixel, bounded to 0.1–1000 m |
| Aperture (f-stop) | Lower values increase blur; independent of exposure |
| Sensor Height (mm) | Filmback height; focal length is inferred from the camera's vertical FOV |
| Max Blur Radius (1080p px) | Pixel radius at 1080p; scales with height, bounded to 64 actual pixels |
| Visualize Circle of Confusion | Red in front of focus, green behind, black at focus |
| Split Diopter | Use the primary focus distance on the left and an independent far focus distance on the right |
| Right Focus Distance (m) | Camera-forward focus depth for the right side; may be nearer or farther than the primary focus |
| Split Position / Split Feather | Normalized horizontal boundary and full transition width; zero feather makes a hard split |

Split diopter mode runs the existing layer-aware blur independently for both lenses, then blends
the completed HDR images across the boundary. Blending signed CoCs before gathering would create
false focus where opposite signs cancel, and break CoC-based foreground ordering across the split.
The two chains share scratch targets in view order, with one extra full-resolution output allocated
only when split mode is first used. Split mode costs fourteen draws instead of seven; ordinary DoF
keeps its single chain. The extra target is released on resize/recreation/destruction.
Positions 0 and 1 select the entire right and left image respectively, including with feather enabled.
It is an image-space approximation; objects crossing the boundary show a focus transition. In
MitchGame, `Assets/Scenes/Showcase/SplitDiopter.lvl` stages an Office Pack employee in a doorway
on the left and his boss behind a desk on the right. See its companion `.md` for comparison settings.

A wide field of view and a distant subject naturally produce little blur. For an obvious test,
use the supplied 20-degree camera at f/1 and focus at 8 m. Narrowing the FOV changes both framing
and focal length. Moving the camera changes distances to every object.

Focus distance is world-space camera depth, interpreted as metres, rather than a screen-space
distance. For a portrait, use the depth of the person's eyes/face along the camera's forward axis;
their entity origin may be at their feet or elsewhere. Model/world scale therefore matters.
The focus control uses a fine drag instead of mapping the entire 0.1–1000 m range onto a slider.
Alt slows the drag, Shift speeds it up, and Ctrl-click allows direct numeric input. The same control
appears in camera properties and Render Settings.

## Implementation

`Modules/Moonlight/Source/Camera/DepthOfField.h` converts the filmback, projection and f-stop to a thin-lens radius:

```
f = sensorHeight * projectionY / 2
radius(z) = f² * imageHeight / (2 * fNumber * (focus - f) * sensorHeight) * (1 - focus/z)
```

Sensor height and focal length use metres in this equation. Focus is constrained beyond the lens.
Negative radii identify the foreground. Radius and user ranges are bounded before upload.

Seven draws run after the HDR shader graphs and temporal AA, before bloom and tonemapping:

1. Full-resolution signed CoC from sampleable depth. TAA's jitter offsets the depth lookup to
   match its unjittered colour output. Both bgfx depth ranges are supported.
2. Half-resolution 2×2 colour/CoC prefilter; mixed blocks retain the foreground and filter colour
   from the selected layer. Odd dimensions round upward and clamp their last source block.
3. Horizontal conservative foreground-radius propagation.
4. Vertical conservative foreground-radius propagation.
5. Half-resolution far gather, rejecting foreground and sources whose disks cannot reach the
   destination. At near silhouettes it estimates hidden background separately, accepting surfaces
   behind the occluder by relative CoC. Two traversals of the disk estimate typical background
   luminance in log space, then apply bounded outlier weights. This prevents sharp HDR points from
   producing displaced copies or sampling-pattern speckles inside blurred foreground objects;
   visible highlights and ordinary far-field bokeh retain their HDR colour.
6. Half-resolution near gather, integrating source-disk coverage and weighting by inverse disk area.
   This produces premultiplied foreground colour and opacity that can spread across focused edges.
7. Composite near over far and the original HDR image, using full-resolution CoC to retain focused
   detail. The debug setting displays CoC instead.

Both gathers integrate every half-resolution source texel in the circular footprint. This avoids
the repeated outlines and point copies produced by a sparse, fixed 64-tap pattern at large radii.
Each tap retains its own signed CoC and colour; bilinearly filtering them together would mix sharp
HDR background into foreground layers. Foreground coverage uses the actual texel area divided by
the circular source-disk area, including odd resolutions. The 64-pixel radius cap bounds the search
to 33 half-resolution texels on each side; circular rejection skips taps outside the footprint.
Large radii cost more texture reads than small radii. Hidden-background reconstruction still uses
two deterministic 64-tap Vogel traversals for its robust colour estimate.
There is no bokeh history, so the result stays stable even with TAA disabled. Each camera owns its targets. No DoF target allocation
or draw occurs when disabled, at zero maximum radius, or for an orthographic camera. Targets are
released with the other camera resources on resize/recreation/destruction.

## Test scene and scripts (MitchGame)

Open `../Assets/Scenes/Tests/DepthOfFieldTest.lvl` and press Play. Red, cyan and gold striped targets
sit at 3.5, 8 and 18 m; foreground bars cross other depths, and a grid of HDR spheres exposes bokeh.
The `DepthOfFieldTest` C# script supplies focus/aperture buttons, a DoF comparison toggle, the CoC
view and an optional rack-focus animation. Its animation is off by default so captures repeat.
`../Tools/BuildDepthOfFieldTest.py` regenerates the scene without external art assets.

From the game root, inside its development environment:

```bash
./.build/Game_linux_Debug/MitchEngine_Tests -tc='Depth of field:*'
python3 Engine/Tools/RunEditorFlows.py --editor .build/Editor_Debug/Havana --wrapper Engine/Tools/Headless.sh --only DepthOfField
python3 Tools/TestDepthOfField.py
python3 Tools/TestDepthOfFieldBlur.py
```

The unit tests check the physical focus/radius relationships, bounded lens parameters, defaults and
serialization. The editor flow checks undo/redo and save/load, captures near/middle/far/CoC/off/TAA
states, recreates active DoF/TAA targets through a viewport resize, and exercises zero-radius and
orthographic bypasses. The image test captures the game without
its control panel and measures focused versus defocused stripe detail, CoC signs, exact bypass
equivalence, TAA and odd-sized output. Its images and logs land in `.tmp/DepthOfFieldImages`.
Far-focus HDR regressions compare otherwise identical scenes with point emission on and off at
960×540 and 1600×900, checking for displaced highlights on the red bars while preserving the
brightness of visible focused lights. An extended bright backdrop verifies that background filling
still works behind the bars.

`TestDepthOfFieldBlur.py` additionally measures a tiny HDR source against a coplanar black surface.
Both foreground and background blur must produce a filled circular disk with a uniform interior,
the expected radial energy profile, and no light outside the aperture footprint. It exercises odd
and even targets and radii from 16 to the maximum 64 pixels. Narrow output windows keep the
maximum-radius fixture inexpensive. Artifacts land in `.tmp/DepthOfFieldBlur/`.

## Comparison and limits

The [2015 GTA V study](https://www.adriancourreges.com/blog/2015/11/02/gta-v-graphics-study-part-3/)
describes signed CoC, foreground-mask spreading and a full-resolution, separable compute blur.
The distinction between near and far blur remains useful. This implementation keeps that principle
but uses circular disk gathers instead of a separable colour kernel, at half resolution.

[AMD FidelityFX DoF](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/depth-of-field/) adds tile
classification, ring-based occlusion, mip/ring merging and more elaborate filtering for large blur
radii and difficult layer boundaries.
[Unreal's cinematic DoF](https://dev.epicgames.com/documentation/en-us/unreal-engine/cinematic-depth-of-field-in-unreal-engine)
also offers diaphragm blade and curvature controls. This implementation supports circular bokeh.
These are capability comparisons, not measured GPU performance rankings.

Current limits:

- A single depth layer cannot recover fully hidden background. Filling near edges uses surrounding
  visible colour; wide foreground objects can still show incomplete or stretched background.
- Transparencies, particles and volumetrics use the opaque depth behind them, so their independent
  focus distances cannot be represented correctly.
- Half-resolution filtering can lose subpixel detail and bright points. Large-radius gathers cost
  proportionally more work; no temporal bokeh accumulation, mip acceleration or adaptive tiles exist.
- Ordinary perspective depth is supported. Oblique portal clip planes need full inverse-projection
  reconstruction before their depth can be interpreted as a lens distance.
- Vulkan, OpenGL and Metal shaders compile locally through bgfx shaderc. Runtime tests use Linux
  Vulkan; native D3D/Metal runtime validation requires those platforms.

Correct visibility through an aperture requires additional layers or lens-sampled rendering.
