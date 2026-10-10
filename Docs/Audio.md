# Audio

Audio runs on FMOD Core through the engine-owned `AudioCore`. The building blocks:

- **Sources:** `AudioSource` components play clips on mixer buses, either in 2D or positioned in 3D at their entity with distance rolloff and doppler.
- **Listener:** an `AudioListener` component, or the camera when there is none.
- **One-shots:** fire-and-forget sounds (`AudioCore::PlayOneShot`, `AudioSource::PlayOneShot`, `PlayAudioEvent`). Each gets its own voice, so repeats overlap instead of restarting.
- **Buses:** Master > Music / SFX / UI / Voice. Their volumes live in Project Settings. Music, SFX and Voice pause with the game.
- **Environment:** `AudioReverbZone`s give places their acoustics, and colliders between the listener and a 3D source muffle it (occlusion).

Automated runs never use the speakers.

> Verified against engine commit afce7083, 2026-10-09 (overhaul Wave 4); reverb zones and occlusion against 21312974, 2026-10-10.

## Overview

| Concept | Type | Notes |
|---------|------|-------|
| System | `AudioCore` | One FMOD system. Owns the bus channel groups, places the listener, syncs sources, updates FMOD |
| Source | `AudioSource` component | Clip, bus, volume, pitch, loop, spatial settings. `Play` / `PlayOneShot` / `Stop` / `Pause` |
| Listener | `AudioListener` component | The first active one wins. Fallbacks: the game camera, or the editor camera in edit mode |
| Voice | `AudioVoice` | A handle to one playing instance. Safe to keep after it ends |
| Bus | `AudioBus` | `Master`, `Music`, `SFX`, `UI`, `Voice`. Each is a channel group under the master group |
| Clip | `Sound` resource | An FMOD sound, cached by path in `ResourceCache` |

Units are metres and seconds. The 3D space matches the engine's: left-handed, +Y up, +Z forward, which is FMOD's default.

## Key Files

| Path | Role |
|------|------|
| `Source/Cores/AudioCore.h` / `.cpp` | The core: FMOD setup and output modes, buses, listener, source sync, one-shots, events, shutdown; `AudioVoice` |
| `Source/Audio/AudioTypes.h` | `AudioBus`, `AudioRolloff`, `AudioOutput`, `AudioPlayParams`, `AudioVoice` |
| `Source/Components/Audio/AudioSource.h` / `.cpp` | Reflected source component and its playback API; `wav` / `mp3` metadata |
| `Source/Components/Audio/AudioListener.h` | Listener marker component |
| `Source/Components/Audio/AudioReverbZone.h` / `.cpp` | Reverb zone component (`ReverbPreset`, min / max distance) |
| `Source/Audio/AudioOcclusion.h` / `.cpp` | `CountAudioObstacles`: the engine's physics-backed occlusion query |
| `Source/Resources/SoundResource.h` / `.cpp` | `Sound` resource (`Release()` frees it before its system goes away) |
| `Source/Events/AudioEvents.h` | `PlayAudioEvent` / `StopAudioEvent` |
| `Source/Engine/ProjectSettings.h` | `BusVolumes`, saved in the project's `ProjectSettings.json` under `Assets/Config` |
| `Tests/Source/AudioTests.cpp` | Behavioural tests on FMOD's non-realtime silent output |

## How It Works

### Output

`Engine::Init` creates `AudioCore` with an output mode:

| Mode | When | FMOD output |
|------|------|-------------|
| `Device` | Interactive runs | The default device. If it can't be opened, falls back to `Silent` with a warning |
| `Silent` | `--no-audio`, or any unattended run (`--frames`, `--screenshot`, `--editor-exec`, …; `AutomationRunner::IsUnattendedRun`) unless `--audio` is passed | `FMOD_OUTPUTTYPE_NOSOUND`: mixes in real time, plays nothing |
| `Manual` | Unit tests | `FMOD_OUTPUTTYPE_NOSOUND_NRT`: mixes only on update, deterministic |

The chosen mode is logged at startup (`Audio: FMOD output …`). `IsSilent()` reports it. The Project Settings Audio section shows a note when audio is silent. FMOD's logging build writes warnings and errors to `fmod_log.txt`.

### Each frame

`AudioCore::Update` runs in `Engine::Run` after gameplay and `AnimationCore`, so sources on animated bones are heard where they're drawn. It calls `Tick( unscaled delta, paused )`:

1. **Pause.** When the engine pause state changes, the Music, SFX and Voice buses pause or resume. UI keeps playing.
2. **Listener.** The core finds the listener, in this order:
   1. the first active `AudioListener` (play mode / game builds);
   2. `Camera::CurrentCamera`;
   3. in edit mode, `Camera::EditorCamera`, so previews pan as you fly around.

   Its position, `Front()`, `Up()` and velocity (from the position change) go to FMOD.
3. **Sources.** For every `AudioSource` in the world:
   - It (re)loads the clip if needed, including when `FilePath` changed in place.
   - It starts a pending play: `PlayOnAwake` when the world starts or when the source is spawned during play, or a `Play()` issued before the clip loaded.
   - It updates the source's occlusion (see Occlusion below).
   - It applies the component's current bus, volume (0 when muted), pitch, loop, spatial blend, min/max distance, rolloff, doppler, occlusion, reverb send and world position/velocity to the source's voice and to its live one-shots.
4. **Reverb zones.** Each active `AudioReverbZone` keeps an FMOD `Reverb3D` at its entity, with its preset and distances (see below).
5. **FMOD update.**

Velocities faster than sound (340 m/s) are treated as teleports (scene loads, camera snaps) and zeroed, so they don't produce a doppler blip.

### Voices and modes

`AudioCore::Play` starts a voice paused, applies `AudioPlayParams`, then unpauses it, so nothing is heard at the wrong place or level. `ApplyParams` only changes the FMOD mode when the voice's loop setting, 2D/3D state or rolloff differs.

| Spatial blend | FMOD mode | Behaviour |
|---------------|-----------|-----------|
| `0` | `FMOD_2D` | No panning, no distance fade |
| `(0, 1]` | `FMOD_3D` + `set3DLevel(blend)` | Panned and attenuated by the rolloff curve between `MinDistance` and `MaxDistance` |

| Rolloff | Curve |
|---------|-------|
| `Inverse` | Full volume inside `MinDistance`, halving with each doubling of distance. It stops fading at `MaxDistance` (it never reaches silence) |
| `Linear` | Full at `MinDistance`, silent at `MaxDistance` |
| `LinearSquare` | Like Linear, falling off faster toward `MaxDistance` |

`Priority` (0 = most important, up to 256) decides which voices go virtual first when FMOD's 512 real voices run out.

### One-shots

- **Core one-shots.** `AudioCore::PlayOneShot( clip, params, maxVoices = 16 )` loads the clip through the cache and starts a voice. More than `maxVoices` live voices of the same clip stop the oldest.
- **Source one-shots.** `AudioSource::PlayOneShot( volumeScale )` (or with another clip) plays at the source with its settings. These voices follow the source every frame. `MaxOneShots` (default 8) is the per-source limit.
- **`PlayAudioEvent`.**
  - Without a `Callback`, it is a core one-shot. It has a `Bus`, `Volume`, an optional `Spatial` + `Position`, and a `StartPercent`.
  - With a `Callback`, it keeps the older behaviour: one cached `AudioSource` per path, restarted on replay and handed to the callback. The asset inspector's preview uses this, and so does `BasicUIView::PlaySound` when its view has a callback set.
- **`StopAudioEvent`** stops both the cached source and the one-shots of that clip.

### Reverb zones

An `AudioReverbZone` gives a place its acoustics. `Preset` picks one of FMOD's 23 presets (Room, Cave, Hangar, Forest, Sewer Pipe, Underwater…). The reverb is fully heard while the listener is within `MinDistance` of the entity and fades out by `MaxDistance`. FMOD blends overlapping zones by the listener's position, and outside every zone there's no reverb.

- **What feeds it:** each voice sends `ReverbMix × SpatialBlend` to the reverb. 3D sounds echo, while 2D ones (music, UI, 2D one-shots) stay dry.
- **Lifecycle:** the core creates the `Reverb3D` on the zone's first tick. It follows the entity's position and the preset, is inactive while the entity or component is disabled, and is released when the zone leaves the world or audio shuts down.
- **Editor:** an inspected zone draws its two spheres.

### Occlusion

A 3D source whose line to the listener is blocked sounds muffled. FMOD's 3D occlusion attenuates the direct sound and adds a lowpass (the system is initialised with `FMOD_INIT_CHANNEL_LOWPASS`); the reverb send is occluded half as much.

- **Query:** every `AudioCore::kOcclusionInterval` (0.1 s) per playing source, `AudioCore`'s occlusion query counts the obstacles. The engine's query is `CountAudioObstacles`: a physics `RaycastAll` from the listener to the source, counting distinct objects (hierarchy roots). The source's and the listener's own hierarchies don't count, and triggers never block.
- **Amount:** occlusion = obstacles × `OcclusionAmount` (0.6 by default, so two walls occlude fully), capped at 1. It glides there over about 0.1 s, so passing a pillar doesn't click.
- **Scope:** only the 3D part is occluded (× `SpatialBlend`). Sources opt out with `Occlusion = false`. Without a query (tests, or no physics) nothing is occluded. `AudioSource::GetOcclusion()` reports the current value.

### Lifecycle

| Event | Effect |
|-------|--------|
| A source joins the world (`OnEntityAdded`) | Its clip loads |
| `World::Start` (`OnStart`) | `PlayOnAwake` sources are queued |
| `World::Stop` (`OnStop`) | Every voice stops, cached legacy sources clear, and pause is lifted. Leaving play mode is silent |
| A source leaves the world (`OnEntityRemoved`) | Its voices stop |
| A reverb zone leaves the world | Its FMOD reverb is released |
| `Engine::Shutdown` → `AudioCore::Shutdown` | Every voice stops. Each `Sound` of this system is released and evicted from `ResourceCache` (holders keep an empty `Sound`). The buses and the FMOD system are released |

### Editor

- **Inspector.** `AudioSource` fields are reflected: an asset picker for the clip, and a "3D Sound" category. Edits are undoable and multi-editable. Below the fields are Play / Stop / Pause / One-Shot preview buttons, a seek bar and an audibility meter.
- **Scene view.** It shows audio icons. Selected 3D sources draw their `MinDistance` and `MaxDistance` spheres.
- **Project Settings.** The **Audio** section has a slider per bus. Changes apply live and save on release.
- **Regression script.** `../Assets/Scenes/Tests/AudioFlows.edscript` runs against `AudioTest.lvl`, using `assert-audio Name | 0/1`.

## How to Extend

- **Gameplay sounds:** call `AudioCore::Get()->PlayOneShot( Path( "Assets/Sounds/Hit.wav" ), params )`, or put an `AudioSource` on the entity and call `PlayOneShot()`. Use `AudioPlayParams::SpatialBlend = 1` and `Position` for positional one-shots.
- **Music:** use an `AudioSource` on the `Music` bus, 2D, `Loop`, `PlayOnAwake` and `Stream`. Fade it by animating `Volume`, or with `GetVoice().SetVolume`.
- **A new bus:** add it to `AudioBus` before `Count`, give it a name in `AudioBusName`, and grow `ProjectSettings::kAudioBusCount` (a `static_assert` keeps them in step). Decide whether it pauses with the game in `AudioCore::Tick`.
- **Tests:** construct `AudioCore( AudioOutput::Manual )`, call `Tick` to mix, and read `AudioVoice::GetAudibility()`. It reports the final level after volume, buses, attenuation and occlusion. Give the core a fake `SetOcclusionQuery` to test occlusion.
- **Other occluders:** replace the engine's query (`AudioCore::SetOcclusionQuery`), for example to give materials their own amounts or to use a layer mask.

## Caveats & Fragility

- **Clips load synchronously** as decompressed samples on the main thread, unless the source sets `Stream`: then the source opens its own FMOD stream (read from disk while playing; no load hitch, little memory). A stream feeds one voice, so a streamed source's one-shots load the clip whole. There is no async load of whole clips.
- **One listener.** FMOD supports several (split-screen), but the core places only listener 0.
- **Occlusion is a ray, not propagation.** Sound doesn't bend around corners or through doorways: a source behind a wall is muffled even with an open door beside it. Every collider counts the same (`OcclusionAmount` is per source, not per material).
- **No DSP effects on buses.** Buses have only volume and mute; the reverb is FMOD's single 3D reverb (instance 0).
- **Pitch ignores time scale.** Slow motion doesn't slow sounds down. Pause is the only time effect.
- **`AudioCore::Get()` is a global.** It's the most recently created core. Only one should exist at a time (the engine's, or a test's).
- **FMOD Studio isn't integrated.** There are no banks or events; this is the FMOD Core API only.
- **`ME_FMOD` off** (no SDK at generation time, or headless) compiles every call to a no-op. Sources report not playing.

## Related Docs

- `Docs/Cores-and-Components-Reference.md`: where `AudioCore` sits among the cores
- `Docs/Architecture.md`: the frame loop and command-line flags
- `Docs/Build-System.md`: how `DEFINE_ME_FMOD` is decided
- `Docs/Editor-Havana.md`: Project Settings and the edscript commands
