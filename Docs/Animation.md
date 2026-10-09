# Animation

Skeletal animation runs in three stages:

- **Import:** `ModelResource` turns Assimp animations into `AnimationClip`s, and turns skin weights into a second vertex stream.
- **Playback:** the engine-owned `AnimationCore` plays `Animator` components. It writes poses straight onto the model's **node entities**, which `Model::Init` creates for every node, bones included.
- **Skinning:** `RenderCore` builds a bone palette per skinned `Mesh` from those entities, and the renderer skins on the GPU in the main, shadow and picking passes.

Because bones are ordinary entities, anything parented to a bone (a weapon, a hat, a particle emitter) follows the animation without extra setup. The editor's play snapshot restores the authored pose on Stop.

> Verified against engine commit 07617c5f, 2026-10-09 (overhaul Wave 4).

## Overview

| Concept | Type | Notes |
|---------|------|-------|
| Clip | `Moonlight::AnimationClip` | Name, duration in seconds, one `AnimationChannel` per animated node (position / rotation / scale keys with their own times) |
| Player | `Animator` component | On the entity whose descendants the clips animate (usually the `Model` entity) |
| Playback system | `AnimationCore` | Engine-owned. Binds, steps the state machine, samples on the job system, writes Transforms |
| State | `AnimatorState` | One clip, or a 1D blend over `BlendClips` by a float parameter. Has speed and loop settings |
| Transition | `AnimatorTransition` | From (or any state) → To. Condition on a parameter, optional exit time, cross-fade duration |
| Parameter | `AnimatorParameter` | Float, Bool or Trigger (a trigger is consumed by the transition that fires on it) |
| Event marker | `AnimatorEventMarker` | Normalized time in a state. Fires an `AnimationEvent` each time playback passes it |
| Skinned mesh | `Mesh` with `MeshData::IsSkinned()` | Up to 128 bones and 4 weights per vertex |

## Key Files

| Path | Role |
|------|------|
| `Modules/Moonlight/Source/Scene/AnimationClip.h` / `.cpp` | Clip and channel data; `AnimationClip::Sample` (linear position/scale, slerped rotation, clamped at the ends) |
| `Modules/Moonlight/Source/Graphics/ModelResource.cpp` | `ProcessAnimations` (keys converted to seconds), `ProcessSkin` (top-4 weights, offsets, bone radii) |
| `Modules/Moonlight/Source/Graphics/MeshData.h` | `kMaxBones`, `BoneNames`, `BoneOffsets`, `BoneRadii`, the skin vertex buffer (`InitSkin` / `GetSkinBuffer`) |
| `Modules/Moonlight/Source/Graphics/ShaderStructures.h` | `SkinWeightsVertex`: 4 × `uint8` indices (normalized) + 4 × float weights, stream 1 |
| `Source/Components/Animation/Animator.h` / `.cpp` | Component, state-machine data, runtime API, `AnimationEvent` |
| `Source/Cores/AnimationCore.h` / `.cpp` | Binding, state machine, sampling, pose writing |
| `Source/Components/Graphics/Mesh.cpp` | `ResolveBones`, `UpdateSkin` (palette + posed bounds), `GetWorldBounds` |
| `Assets/Shaders/Skinning.sh` | `u_bones[128]` and `skinMatrix(indices, weights)` |
| `Assets/Shaders/StandardSkinned.vert`, `ShadowDepthSkinned.vert`, `Picking/picking_skinned.vert` | Skinned variants of the main, shadow and picking vertex shaders |
| `Tests/Source/AnimationTests.cpp` | Sampling, playback, transitions, blends, events |

## How It Works

### Import

`ModelResource::Load` (and the `.assbin` export in `ModelResourceMetadata::Export`) read with `aiProcess_LimitBoneWeights` in addition to the usual flags.

- **Clips.** `ProcessAnimations` creates one `AnimationClip` per `aiAnimation`. Key times are divided by `mTicksPerSecond` (25 when the file leaves it at 0). An unnamed animation is called `Animation N`.
- **Skin.** `ProcessSkin` handles every mesh with bones:
  - It keeps the four strongest influences per vertex, normalizes them, and uploads them as vertex stream 1.
  - It records each bone's name and offset (inverse bind) matrix.
  - It records a **reach radius**: the largest distance from the bone to a vertex it moves by more than 5%.
  - A mesh with more than `MeshData::kMaxBones` (128) bones logs a warning and stays unskinned.

### Binding

`AnimationCore::Bind` runs on an Animator's first update after `World::Start`. `OnStart` unbinds every Animator, so the bind pose is the scene as authored.

1. **Find the clips.** In order:
   1. clips handed over with `Animator::UseClips` (procedural or code-built animation);
   2. the clips of the model at `ClipSource`, which lets several characters share one rig's clips;
   3. the clips of the `Model` on the same entity.
2. **Build the states.** If `States` is empty, every clip becomes a state of the same name.
3. **Bind channels to nodes.** Each channel's `NodeName` is matched against the Animator entity and its descendants. A name found more than once binds to its first match, with ancestors before descendants. Each binding remembers the node's local position, rotation and scale as its bind pose.
4. **Start playing.** With `PlayOnStart`, `DefaultState` starts (the first state when empty).

### Each frame

`AnimationCore::Update` runs in `Engine::Run` after `Game::OnUpdate` and the audio update, and before particles and `RenderCore`. Gameplay therefore sets parameters for the same frame's pose. It only advances while the world is started (play mode, or game builds).

1. **State machine** (main thread, per Animator):
   1. A pending `Play()` applies first. A name that is a clip but not a state becomes an implicit state.
   2. If no cross-fade is running, the first matching transition fires. Transitions are checked in declaration order.
   3. Time advances by `delta × Animator::Speed × state Speed`. Non-looping states clamp at their end.
   4. Event markers passed during the step fire `AnimationEvent { Entity, State, Name }`, once per lap.
2. **Sampling** (`ParallelFor` over the playing Animators):
   - **Starting pose.** Each binding starts from its bind pose, and keyed properties override it. A clip with only rotation keys therefore keeps the authored positions.
   - **1D blends.** The two `BlendClips` around the parameter value are blended. Their thresholds are sorted at sample time, and the clips stay in phase by normalized time.
   - **Cross-fades.** The previous state is blended in with a smoothstep weight over the transition's `Duration`.
3. **Write** (serial): `SetPosition` / `SetRotation` / `SetScale` on each node's `Transform`.

`Animator::Stop()` freezes the pose. Setting `Speed` to 0 does the same but keeps transitions and events live.

### Transition conditions

| Condition | Passes when |
|-----------|-------------|
| `Always` | Always. Combine with `ExitTime` for "when the clip finishes" |
| `Greater` / `Less` | The parameter is above / below `Threshold` |
| `True` / `False` | The parameter is non-zero / zero (a missing parameter counts as false) |
| `Trigger` | The trigger is set. Firing clears it |

`ExitTime ≥ 0` also requires the source state's normalized time to have reached it. Looping states count past 1, so 1.0 means "after one full play".

### Skinning

Skinning runs in two places:

- **CPU side**, inside `RenderCore`'s parallel mesh job, which runs after `Transform::UpdateAll` so world matrices are clean. `Mesh::UpdateSkin` does the work:
  - It resolves bone entities once, through `ResolveBones`. The search is by name, depth-first below the nearest ancestor with a `Model`. A missing bone falls back to that root.
  - It writes the palette `inverse(meshWorld) × boneWorld × offset` for each bone.
  - It returns world bounds built from each bone's reach sphere. Culling, the editor's selection bounds and Frame Selected all use these bounds, through `Mesh::GetWorldBounds`.
- **Renderer side.** The `MeshCommand` carries `SkinPalette` / `SkinBoneCount`, and skinned meshes opt out of instancing. `BGFXRenderer::RenderSkinnedMesh` draws each one with a one-entry instance buffer, the skin stream on vertex stream 1, and `u_bones`. The other passes do the same:
  - Shadow casters (sun cascades and spot lights) use `ShadowDepthSkinned`.
  - The editor picking pass uses `picking_skinned`. A click therefore selects what is drawn, not the bind pose.

`Material::GetSkinnedProgram()` returns the skinned program. `StandardMaterial` loads `StandardSkinned.vert` + `Standard.frag` lazily. Materials that don't override it draw skinned meshes in the bind pose.

Bone indices are stored as normalized `Uint8`. `skinMatrix` multiplies them by 255. A vertex with no weights keeps the identity and stays rigid with the mesh.

### Editor

- **Inspector.** `Animator` fields are reflected: clip source, default state, speed, and the state machine (parameters, states, transitions, events), editable like any other component. During play the inspector also shows the current state, time, blend status and animated-node count, and a clip list (clicking a clip plays it with a 0.2 s fade).
- **Regression script.** `../Assets/Scenes/Tests/AnimationFlows.edscript` runs against `AnimationTest.lvl`. It checks:
  - the bind pose holds in edit mode;
  - posed skinned meshes frame and pick correctly;
  - the bones move in play;
  - the hat stays on the tip bone;
  - Stop restores the bind pose.

## How to Extend

- **Drive an Animator from gameplay:** call `SetFloat` / `SetBool` / `SetTrigger` in `OnUpdate`, and `Play("State", fade)` for direct control. Listen for `AnimationEvent` (an `EventReceiver` registered for `AnimationEvent::GetEventId()`) for footsteps, hit frames and similar.
- **Procedural clips:** build `Moonlight::AnimationClip`s in code and pass them with `UseClips`. The unit tests do exactly this.
- **Attach props:** parent the prop entity to the bone entity (found by name under the model). It follows the pose with no further setup.
- **A new skinned material:** override `GetSkinnedProgram()`. Pair a vertex shader that includes `Skinning.sh` and declares `a_indices` / `a_weight` in its `.var` with the material's fragment shader.

## Caveats & Fragility

- **No root motion, 2D blend trees, layers or masks.** One state machine drives the whole hierarchy. Additive and partial-body animation aren't supported.
- **No edit-mode preview.** Animators only play while the world is started. Edit mode shows whatever pose the bone entities hold. Posing bones by hand in edit mode is saved with the scene.
- **Name-based binding.** Clips and skins find nodes by name. Two descendants with the same name bind to the first one found, and renamed bone entities stop animating or skinning.
- **Linear key interpolation only.** FBX cubic tangents are lost on import. Keys are found by linear scan, so very long clips cost more per sample.
- **Import scale is not converted.** Assets authored in centimetres (e.g. Synty FBX) import at 100× size. Scale the entity, since there is no unit conversion on import yet.
- **Linux Assimp importers:** the prebuilt Linux Assimp library includes only the FBX, OBJ and Assbin importers. glTF / GLB models don't load there.
- **Skinned meshes draw one by one.** They aren't instanced. Each one costs one draw per view (camera, shadow cascade, spot light).
- **Bounds come from bone reach spheres.** They are conservative (larger than the mesh). Vertices weighted under 5% don't count toward a bone's reach.

## Related Docs

- `Docs/Rendering-Pipeline.md`: where skinned draws sit in the frame
- `Docs/Cores-and-Components-Reference.md`: `Animator`, `AnimationCore`, `Model`, `Mesh`
- `Docs/Resources-and-Assets.md`: model import and `.assbin` cooking
- `Docs/Editor-Havana.md`: the edscript automation commands
