# Hardware Profiles and Budgets

A project picks a **hardware profile**: a target machine such as "Xbox 360", "2004 PC", "Switch" or "Steam Deck". The profile sets a frame-time target, memory budgets, per-frame draw limits and the GPU features the target has. `HardwareBudget` measures every frame against it, and the editor shows the verdict. When the profile is **enforced**, the renderer turns off the features the target lacks and caps texture and shadow sizes, so the game shows the target's look. Profiles are JSON data.

> Verified against engine commit 3f04199c plus the hardware profiles change, 2026-10-22; `MaxVolumetricSteps` against 94407e9d plus the concert lighting change, 2026-10-29.

## Overview

- **Profiles** (`HardwareProfile`) come from the engine's `Assets/Config/HardwareProfiles.json` (four built-in targets), then from the project's `Assets/Config/HardwareProfiles.json`.
- **The choice** is per project: `"HardwareProfile": { "Name", "Enforce" }` in `ProjectSettings.json`, edited in Project Settings > Hardware. `--hardware-profile "Name"` and `--enforce-profile` override it for one run; `--no-profile-enforcement` turns enforcement off whatever else asks for it. Every run logs the result (`Hardware profile: none`, `Hardware profile: Xbox 360 (enforced)`). Screenshot captures (`Tools/ScreenshotRegression.py`) pass `--no-profile-enforcement` unless their `--game-args` has `--enforce-profile`, and print each scene's profile, so a local `ProjectSettings.json` edit can't leak into references (ME-255); `--update` also warns while `Assets/Config` has uncommitted changes.
- **Measurement** happens once a frame, right after `FrameStats::EndFrame` (`Docs/Architecture.md`). It reads `FrameStats`, which the renderer fills in `BGFXRenderer::GatherFrameStats`, plus process and FMOD memory a few times a second.
- **Enforcement** writes `BGFXRenderer::FeatureCaps` and `Moonlight::Texture::SetMaxUploadSize`. The caps mask the renderer's settings and each camera's post-processing while frames render; they never change the authored settings. Lifting a cap brings the authored look back.

## Key Files

| Path | Role |
|------|------|
| `Source/Engine/HardwareBudget.h` / `Source/Engine/HardwareBudget.cpp` | `HardwareProfile` (data + JSON), `HardwareBudget` (profile list, active profile, per-frame lines, warnings, caps, overlay, report JSON) |
| `Assets/Config/HardwareProfiles.json` | The built-in profiles |
| `Source/Engine/ProjectSettings.h` | `HardwareProfileName`, `EnforceHardwareProfile` |
| `Modules/Moonlight/Source/Renderer.h` / `Modules/Moonlight/Source/Renderer.cpp` | `FeatureCapSettings`; scene draw counting (`SceneCounts`, `CountTriangles`); the new `RenderStats` fields in `GatherFrameStats` |
| `Modules/Moonlight/Source/Graphics/Texture.cpp` | The texture size cap (`FinishAsyncLoad` skips top mips) |
| `Modules/Moonlight/Source/Graphics/MeshData.h` | `MeshData::GetTotalGpuBytes` (vertex and index buffers of live meshes) |
| `Modules/Dementia/Source/Profiling/FrameStats.h` | `RenderStats`: texture / render target / mesh memory, scene draws, triangles, size, lights |
| `Modules/Dementia/Source/Utils/PlatformUtils.cpp` | `GetProcessMemoryBytes` (working set: Win64, macOS, Linux; 0 elsewhere) |
| `Source/Cores/AudioCore.cpp` | `AudioCore::GetMemoryUsage` (FMOD's current allocation) |
| `Modules/Havana/Source/Widgets/SettingsWidgets.cpp` | Project Settings > Hardware |
| `Modules/Havana/Source/Widgets/SceneViewWidget.cpp` | The verdict under the World View's and Game View's stats overlay |
| `Tests/Source/HardwareBudgetTests.cpp` | Profile parsing, replace / `Extends`, line statuses and peaks, formatting, the enforced texture cap, mip-skipping uploads |

## How It Works

### Profile data

```json
{
    "Profiles": [
        {
            "Name": "Xbox 360",
            "Description": "...",
            "ShaderModel": "Shader Model 3.0+ (Xenos, unified shaders)",
            "TargetFps": 30,
            "Resolution": [ 1280, 720 ],
            "UnifiedMemory": true,
            "RamMiB": 480,
            "TextureMiB": 200, "MeshMiB": 64, "RenderTargetMiB": 48, "AudioMiB": 48,
            "MaxDrawCalls": 2500, "MaxTriangles": 1000000, "MaxLights": 8,
            "Caps": { "MaxTextureSize": 2048, "MaxShadowResolution": 1024, "VolumetricFog": false, "TemporalAA": false }
        }
    ]
}
```

- A missing key or 0 means no limit, and a missing cap means the target has the feature.
- `CpuMs` / `GpuMs` default to the frame target, `1000 / TargetFps`.
- `UnifiedMemory` targets have one pool, so process memory plus GPU memory count against `RamMiB`, and `VramMiB` is ignored.
- `ShaderModel` is informational.
- The project file loads after the engine's. A profile there with an existing `Name` replaces that profile, and `"Extends": "Name"` starts from an earlier profile's values. `HardwareBudget::AddProfiles` adds a document the same way from code.
- The figures are budgets for a game on that machine, not hardware maximums. For example, the Xbox 360's texture cap is 2048 although its GPU accepts 8192.

### The budget lines

`HardwareBudget::RebuildLines` makes one `Line` per budget the profile limits. `Update` refreshes them each frame:

| Line | Measured from |
|------|---------------|
| CPU frame | `FrameStats::GetSmoothedFrameMilliseconds` (main-thread work, the frame limiter's sleep excluded) |
| GPU frame | `RenderStats::GpuMilliseconds`, smoothed the same way (0.1 EMA) |
| Draw calls / Triangles | `RenderStats::SceneDrawCalls` / `SceneTriangles`: the mesh draws (every `CountTriangles` call) of the game's cameras, their sun cascades and the frame's local shadow passes |
| Lights | `RenderStats::Lights` (this frame's `LightCommand`s) |
| Resolution | `RenderStats::SceneWidth` × `SceneHeight`: the main game camera's output, compared as pixel counts |
| Memory | `PlatformUtils::GetProcessMemoryBytes`, plus GPU memory on unified targets |
| GPU memory | Textures + render targets + meshes. The driver's `gpuMemoryUsed` isn't used: on shared-memory devices (integrated GPUs, lavapipe) it reports the whole heap's use, other processes included |
| Textures / Render targets | bgfx's `textureMemoryUsed` / `rtMemoryUsed` estimates (every backend) |
| Meshes | `MeshData::GetTotalGpuBytes`: vertex, index, skin-weight and LOD buffers of live meshes and shared topologies (wireframe views not counted) |
| Audio | `AudioCore::GetMemoryUsage` (`FMOD::Memory_GetStats`) |

A line is **Near** at 90% of its limit and **Over** above it. `Peak` holds the highest value since the profile was picked or `ResetPeaks`. A line that stays over for a second logs one warning (`[Budget] Xbox 360: Draw calls 2,431 is over 2,500`). It logs again only after it drops below 95% of the limit, so a value hovering at the limit doesn't flood the log.

**Editor views are left out of the draw counts.** `RenderCameraView` points `m_counting` at the game or the editor tally by `CameraData::IsEditorView`, and `GatherFrameStats` reports the game tally when any game camera rendered. When none did, it reports the scene view's tally. Fullscreen, post-processing and UI passes are not counted.

### Enforcement

`HardwareBudget::ApplyCaps` runs when the profile, the enforce flag or the profile files change:

- **Renderer** (`BGFXRenderer::FeatureCaps`):
  - `Shadows` off masks `Shadows.Enabled` (`AreShadowsActive`).
  - `MaxShadowResolution` clamps the cascade, spot and point tile sizes (`CapShadowResolution`); the atlases are recreated at the clamped size.
  - `EnvironmentLighting` off skips the IBL probe, so ambient falls back to the fallback cube.
  - `RenderCameraView` clears `AmbientOcclusion`, `Bloom`, `DepthOfField`, `VolumetricFog` and `TemporalAA` from the camera's post settings for that render, and clamps `VolumetricSteps` to `MaxVolumetricSteps` (0 = no cap; the Steam Deck profile sets 64). `CameraRestore` puts them back afterwards.
  - `Instancing` off limits every instanced submit, opaque and shadow casters, to one instance per draw. The draw count rises the way it would on hardware without instancing, and every mesh keeps its shader. Particles stay batched.
- **Textures** (`Texture::SetMaxUploadSize`): `FinishAsyncLoad` uploads a single-layer mipped 2D texture from its first mip that fits the cap. `mWidth` / `mHeight` report that size, so decals sample with the right texel size. Changing the cap reloads every cached file texture that has a compiled `.dds`.

**Quality presets** (`QualityPresets` in the same files, picked by the player's `Graphics.Quality` setting) are a second cap set. `GetEffectiveCaps` combines them with the enforced profile's: a feature stays on only if both allow it, and a size cap takes the smaller nonzero limit (`HardwareProfile::Combine`). See `Docs/Player-Settings.md`.

Without enforcement the Hardware tab still reports what exceeds the target: cameras using effects it lacks, shadow resolutions above the cap, and loaded textures larger than the size cap.

### Where it shows

- **Project Settings > Hardware** has the profile picker, the enforce checkbox, and live budget bars with peaks and limits. A Feature Caps table shows each cap, the target's value and the project's use. It also offers profile file actions: Reload, and Create or Edit Project Profiles (which writes a starter project profile that `Extends` the current one).
- **Stats overlay** (World View and Game View): a box under the stats with the profile's verdict ("Xbox 360: 2 over budget") and only the lines that are near or over.
- **Game builds**: `--budget-hud` draws the full overlay in the top-right corner (ImGui builds).
- **Automation**: with a profile active, `--perf-report` gains a `"Budgets"` object with the profile, `Enforced`, `OverCount`, `PeakOverCount`, and each line's `Used`, `Peak`, `Limit`, `Unit` and `Status`. Peaks reset when the warm-up ends, so loading doesn't count.

```bash
./.build/Editor_Debug/Havana --scene Assets/Example.lvl --frames 600 \
    --hardware-profile "Xbox 360" --enforce-profile --perf-report .tmp/x360.json
```

## How to Extend

**Add a target:** add an entry to the project's `Assets/Config/HardwareProfiles.json` (Project Settings > Hardware > Create Project Profiles writes one). Add it to the engine's file only if every project should see it.

**Add a budget line:**

1. Add the measurement to `FrameStats::RenderStats`, or sample it in `HardwareBudget::SampleMemory` if it's expensive.
2. Add a `HardwareBudget::Budget` value and its `kBudgets` entry (same order; a `static_assert` checks the count).
3. Add the profile key to `HardwareProfile` and `Deserialize`.
4. Add the line in `RebuildLines`, and read the measurement in `Update`'s switch.

**Add a feature cap:**

1. Add it to `HardwareProfile::FeatureCaps` and `Deserialize`.
2. Add it to `BGFXRenderer::FeatureCapSettings` and copy it in `HardwareBudget::ApplyCaps`.
3. Mask the setting where the renderer *uses* it, never where it's stored, so lifting the cap restores the authored value.
4. Add a row to the Hardware tab's Feature Caps table.

## Caveats & Fragility

- **Times are this machine's.** CPU and GPU frame times are measured on the machine running the game, and in the editor they include the editor's own work. Draw calls, triangles, lights, resolution and the content memory lines (textures, meshes, render targets, audio) carry over to the target; frame times don't.
- **Process memory includes everything.** In the editor it includes the editor, ImGui, the .NET runtime and driver allocations, and on PC it includes runtime overhead a console doesn't have. Unified-memory targets look worse than they are in the editor. On a shared-memory dev machine (integrated GPU, lavapipe) process memory already holds the GPU allocations, so a unified target's shared line counts them twice.
- **Editor content counts as memory.** Texture and render-target memory include editor icons, thumbnails and the scene view's targets.
- **The texture cap needs mips.** A texture without mips, an array, a cube map or a 3D texture uploads at full size. Textures already loaded keep their size until the cap changes (which reloads them).
- **Reloading textures hitches.** Changing the texture cap reloads every cached texture synchronously on the main thread.
- **Game camera visibility in the editor.** Draw counts follow the game cameras that rendered this frame. When the Game view is hidden they follow the scene view, which can differ from the game camera.
- **Not covered:** shader model and MRT limits (bgfx's shader profiles are per backend), render-resolution enforcement (camera targets match their output size), shadow cascade count (fixed at four), the number of lights (reported, not enforced), and backbuffer MSAA (it doesn't affect scene targets; `Docs/Rendering-Pipeline.md`).

## Related Docs

- `Docs/Architecture.md` — frame loop, `FrameStats`, automation flags
- `Docs/Rendering-Pipeline.md` — shadows, post-processing, instancing, statistics
- `Docs/Resources-and-Assets.md` — texture loading and the resource cache
- `Docs/Editor-Havana.md` — Project Settings, the stats overlay
- `Docs/State-of-the-Engine.md` — what's left
