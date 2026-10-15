# Sponza animation performance

Measured on 2026-10-14 using NVIDIA Vulkan on a private Xvfb display, at 1280×720,
with both editor views visible. The scene, view, lighting, shadow quality, and fixed
simulation step (1/24 s) were held constant. Each window records 120 frames after
warming up; the table reports medians after dropping the first five samples.

| Debug editor | Before | After |
| --- | ---: | ---: |
| Edit frame | 64.14 ms / 15.6 FPS | 21.99 ms / 45.5 FPS |
| Playback frame | 64.83 ms / 15.4 FPS | 24.25 ms / 41.2 FPS |
| Playback render preparation | 22.00 ms | 3.13 ms |
| Playback render submission | 39.86 ms | 19.20 ms |
| Playback render-thread CPU | 11.90 ms | 1.94 ms |
| Playback GPU | 11.77 ms | 6.58 ms |
| Playback draw calls | 12,493 | 3,603 |
| Knight material meshes | 897 | 8 |

The previous bake represented each vertex as a bone and partitioned the knight into
128-influence pieces. That repeated matrix construction, uniform uploads and draw
submission across the camera, shadow and picking passes. The new bake groups by
material and preserves source IDs through cooking. Instances sample disjoint vertex
ranges in parallel, upload their posed buffers once, and reuse them across passes.
Static/paused poses skip sampling and uploads. Animation positions, UV seams,
shading bases, posed bounds and independent instance playback are retained.

A separate optimized editor run measured 23.52 ms / 42.5 FPS during playback, with
render preparation at 0.51 ms. A detailed CPU trace puts roughly 20 ms inside
`bgfx::frame` in this virtual-display setup. These results establish the improvement
in this setup; they are not a prediction of the desktop's maximum frame rate.

The benchmark script is `Tests/Editor/SponzaPerformance.edscript`; it writes
`.tmp/Sponza-edit-profile.json` and `.tmp/Sponza-play-profile.json`. Run it from the
project root with `--scene Assets/Scenes/Tests/Sponza.lvl --frame-time 0.041666667
--editor-exec Engine/Tests/Editor/SponzaPerformance.edscript`. Use the same binary,
viewport size, GPU and camera setup when comparing changes. Raw before/after data
and the detailed trace from this session are in `.tmp/Perf/`.

Validation: 136 unit tests passed. GPU checks covered playback, scrubbing, preview,
Stop restoration, posed picking, and two independent knight instances. The full
Sponza game also rendered and captured successfully on software Vulkan; the much
slower software-editor run was stopped before completion. Debug editor, optimized
editor and Debug game builds were rebuilt.
