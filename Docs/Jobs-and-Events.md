# Jobs and Events

The engine's two cross-cutting runtime services: `Jobs::JobSystem`, a work-stealing job system with counters and `ParallelFor`, and `EventManager`, a typed publish/subscribe bus with immediate and queued delivery. This doc is the **threading rulebook** — what may run on a job and what must stay on the main thread.

> Verified against engine commit dab803a2, 2026-10-08; the "What's threaded today" map against d2c1e6b2, 2026-11-06; the physics task bridge against bdfedae8, 2026-10-09; typed `Listen` receivers against 07595696 plus the typed-events change, 2026-10-16.

## Overview

- `Jobs::JobSystem::Get()` starts `hardware_concurrency - 1` workers on first use. Every thread that isn't a worker shares queue 0; workers pop their own queue newest-first and steal oldest-first from the others, then sleep when idle.
- Waiting on a `Jobs::Counter` *helps* — the waiting thread executes queued jobs — so nested parallelism (waiting inside a job) cannot deadlock.
- Events: `Fire()` dispatches immediately on the main thread; `Queue()` copies the event and delivers it at the start of the next frame and is safe from any thread.

## Key Files

| Path | Role |
|------|------|
| `Modules/Dementia/Source/Jobs/JobSystem.h` / `Modules/Dementia/Source/Jobs/JobSystem.cpp` | `Jobs::JobSystem`, `Jobs::Counter`, `ParallelFor`, `Submit` |
| `Modules/Dementia/Source/Profiling/ThreadName.h` | `ME_PROFILE_THREAD`: a thread's profiler lane and OS name |
| `Source/Cores/PhysicsCore.cpp` | `PhysicsCore::EnqueueTask` / `FinishTask`: Box3D's solver tasks run as jobs (`Docs/Physics.md`) |
| `Modules/Dementia/Source/Events/Event.h` | `Event<T>` CRTP: `GetEventId`, `Fire`, `Queue` |
| `Modules/Dementia/Source/Events/EventManager.h` / `Modules/Dementia/Source/Events/EventManager.cpp` | Receiver registry, dispatch, the cross-thread queue |
| `Modules/Dementia/Source/Events/EventReceiver.h` / `Modules/Dementia/Source/Events/EventReceiver.cpp` | `EventReceiver` (typed `Listen<T>` handlers, auto-deregistering), `EventSubscription` (lambda) |
| `Source/Cores/Rendering/RenderCore.cpp` | Canonical `ParallelFor` user (mesh command build) |
| `Source/Components/Transform.cpp` | `Transform::UpdateAll` (parallel world-matrix resolve) |

## How It Works

### Jobs

```cpp
Jobs::JobSystem& jobs = Jobs::JobSystem::Get();

// Data-parallel loop: batches of >= 128, the calling thread takes the first batch.
jobs.ParallelFor( count, 128, [&]( uint32_t begin, uint32_t end ) {
    for( uint32_t i = begin; i < end; ++i ) { /* ... */ }
} );

// Fire-and-wait tasks.
Jobs::Counter counter;
jobs.Run( counter, []() { /* ... */ } );
jobs.Wait( counter );   // runs other jobs while waiting
```

- `ParallelFor` picks a batch size giving about four batches per thread (never below `minBatchSize`), queues all batches but the first, runs the first itself, then waits. It allocates nothing: jobs point at the caller's callable.
- `Run` heap-allocates the callable (use it for coarse tasks).
- `Submit( counter, fn, context, begin, end )` enqueues a raw range job (for C-style task callbacks).
- `Engine::GetJobSystem()` returns the same instance; `Engine::Shutdown` stops the workers.

### Profiling threads and jobs (ME-321)

Every thread the engine starts shows as a named lane in every capture, even while idle:

- **The rule:** the first line of a thread function (or a `std::async` lambda) is `ME_PROFILE_THREAD( "Name" )` (`Modules/Dementia/Source/Profiling/ThreadName.h`). It registers the profiler lane for the thread's lifetime (`OPTICK_THREAD` is a scoped object, so it must sit in the thread function's own scope, never in a helper) and names the OS thread for gdb and `top -H` (Linux keeps 15 characters). Wrap the thread's work in an `OPTICK_EVENT` too. New threads, pools and `std::async` calls get both in the same change.
- **Jobs:** `JobSystem::Execute` puts a zone around every job, named by `ParallelFor( "Name", count, minBatch, fn )`, `Run( counter, fn, "Name" )` or `Submit( ..., "Name" )` (string literals: kept by pointer). An idle worker's wait is a `Worker Sleep` zone.
- **Linux captures** (`ThirdParty/ChromeTrace/optick.h`, Chrome trace JSON for Perfetto / `chrome://tracing`): registered threads get `thread_name` / `thread_sort_index` metadata and a `Capture` marker at the capture's start, so idle lanes still appear. Order: Main Thread, then lanes in registration order, with numbered names ("Job Worker 1".."N") in numeric order. Threads that exited before the capture started are left out.
- **Registered today:** Main Thread (`Engine::Init`), Job Worker N, Resource Loader N, Reimport N, File Watcher, Script Runtime Init, Script Build, Nav Bake, Nav Carve, Nav Tile Builder, Scene Read, Save Write, Terrain Preview / Page / Stream / Cook. Threads started by FMOD, .NET and the GPU driver aren't the engine's and don't appear.

### Worker utilization (ME-320)

The job system counts who ran jobs, so a frame shows how much of it ran on the workers:

- **Lanes:** lane 0 is `Main` (the main thread, plus any other thread that isn't a worker: they share it) and lanes 1..N are `Job Worker 1..N`. Each lane counts the jobs it ran and its busy time (wall time inside job bodies). A job run inside another job on the same thread (a helping `Wait` in a job) counts as a job, but its time is already inside the outer one, so it isn't counted twice. `ParallelFor`'s first batch, which the caller runs itself, is a job on the caller's lane; a `ParallelFor` small enough for one batch runs inline and isn't counted.
- **Wait:** time the main thread spends in a top-level `Wait` (`ParallelFor`'s included) minus the jobs it ran meanwhile: how long it sat waiting for the workers. Waits inside a job aren't counted.
- **Cost:** two `steady_clock` reads per job and relaxed atomic adds into a cache-line-aligned counter per lane (a worker only writes its own). No locks on the job path.
- **Reading:** `JobSystem::TakeUtilization( Jobs::Utilization& )` copies the counters and zeroes them. `FrameStats::EndFrame` calls it once a frame, so `FrameStats::GetJobStats()` covers exactly one frame: per-lane `BusyMilliseconds`/`Jobs` (and a smoothed value), `WorkerBusyMilliseconds`, `MainBusyMilliseconds`, `MainJobs`, `WaitMilliseconds`, `Jobs`. Other callers of `TakeUtilization` would steal the frame's numbers.
- **JSON:** `FrameStats::ToJson( stats, frameMs )` is the `Workers` block in the edscript `profile-stop` frames and the `--perf-report`: `{ Count, BusyMs: [per worker], Jobs, WaitMs, MainBusyMs, MainJobs, JobsPerWorker: [...], Utilization }`, where `Utilization` is worker busy / (workers x frame ms).
- **Editor:** the World/Game View stats overlay adds `Jobs N  Workers X ms  Wait Y ms` (smoothed), and the Profiler panel's "Job workers" section lists every lane's jobs, ms and share of the frame.
- **Other pools:** every lane carries a `Jobs::LaneKind` (`Main`, `Worker`). A pool with its own threads (the ME-319 background lane) adds a kind and appends its lanes; `Count`/`BusyMs` stay the frame workers.

### What's threaded today (ME-320)

Every place frame work goes to the job workers (`grep -rn "ParallelFor\|\.Submit(" Source Modules`). The last column is a HeroShooter Arena play session (Havana Debug, headless, 32 hardware threads, 120 frames of a Chrome trace): most frames fan out a few hundred jobs of a few microseconds each, about 0.3 ms of worker time in a ~50 ms frame.

| Site | Job name | Runs in parallel | Batching | HeroShooter, per frame |
|------|----------|------------------|----------|------------------------|
| `Transform::UpdateAll` (`Source/Components/Transform.cpp:590`), called by `RenderCore` and `ParticleCore` | `Transform::UpdateAll` | Dirty world matrices, one subtree per item: the root frontier is expanded on the main thread until there are ~4 subtrees per thread | 1 subtree | ~115 jobs, ~3.8 us each |
| Box3D solver (`PhysicsCore::EnqueueTask`, `Source/Cores/PhysicsCore.cpp:281`) | `Physics Task` | Box3D's own task stages (`workerCount` = thread count); the main thread waits in `FinishTask` | 1 Box3D task per job | ~108 jobs, ~0.9 us each (inside `Fixed Update`) |
| Box2D solver (`Physics2DCore::EnqueueTask`, `Source/Cores/Physics2DCore.cpp:295`) | `Physics2D Task` | Box2D's ranged tasks, split into up to thread-count ranges | `InMinRange` | none (no 2D physics) |
| `AnimationCore` (`Source/Cores/AnimationCore.cpp:1149`) | `Animation` | Pose sampling per playing animator with bindings | 1 animator | none (no skinned animators) |
| `ParticleCore` (`Source/Cores/Rendering/ParticleCore.cpp:57`) | `Particles` | Particle simulation per system (textures resolve on the main thread first) | 1 system | none in steady play |
| `RenderCore` vertex animation (`Source/Cores/Rendering/RenderCore.cpp:663`) | `Render Prep: Vertices` | Vertex-animated meshes, 4096 vertices per item | 1 item | none |
| `RenderCore` mesh commands (`Source/Cores/Rendering/RenderCore.cpp:717`) | `Render Prep: Mesh Commands` | Per-mesh command build and per-camera visibility flags | 128 meshes | ~1 job (the scene fits one or two batches) |
| `BGFXRenderer` shadow casters (`Modules/Moonlight/Source/Renderer.cpp:1934`) | `Shadow Caster Chunks` | Shadow-caster lists in 2048-command chunks | 1 chunk | runs inline (one chunk) |
| `BGFXRenderer::CullShadowCasters` (`Modules/Moonlight/Source/Renderer.cpp:2021`) | `Cull Shadow Casters` | Caster vs. cascade/light frustums per chunk | 1 chunk | runs inline (one chunk) |
| `NetReplication` (`Source/Net/NetReplication.cpp:333`) | `Net Snapshot Encode` | One snapshot per client from the shared capture (`Settings.UseJobs`, 2+ clients) | 1 client | server only |

A `ParallelFor` with a single batch runs inline on the caller and creates no job. Outside the job system, `DynamicSky` bakes its lookup tables with its own short-lived `std::thread`s (`Modules/Moonlight/Source/Graphics/DynamicSky.cpp:377`, on sky changes, not per frame), and background work (resource loads, scene reads, saves, terrain pages, nav builds, script builds) runs on its own named threads (see *Profiling threads and jobs*; ME-319 moves them onto one background lane).

Stays on the main thread every frame:

| Work | Where | Why |
|------|-------|-----|
| C# scripts (`OnUpdate`, `OnFixedUpdate`, `OnLateUpdate`) | `Source/Scripting/ScriptEngine.cpp`, `Game Update` / `Fixed Update` scopes | The .NET calls and the World they touch are single-threaded |
| Physics body sync, characters, read-back around `b3World_Step` | `Source/Cores/PhysicsCore.cpp` (`SyncBodies`, `b3World_Step` at :1384) | They write the World; only the solver's stages are jobs |
| Every core's `Update` in order | `World::UpdateLoadedCores` (`Source/Engine/World.cpp:739`) | Cores run one after another; a core may use jobs inside its own update |
| Navigation crowd | `surface.Crowd->update` (`Source/Cores/NavigationCore.cpp:1543`) | One Detour `dtCrowd::update` per surface; not split into jobs |
| UI | RmlUi in `RmlUiCore::Update` (`UI Update`) | RmlUi isn't thread-safe and reads script-set models |
| Audio | `AudioCore` (`Audio` scope) | Listener/emitter updates walk the World; FMOD mixes on its own threads |
| bgfx submission and `bgfx::frame()` | `BGFXRenderer::Render` (`Render Submit`; `Renderer.cpp:695`) | bgfx runs single-threaded (`bgfx::renderFrame` before `init`), so `frame()` renders on the main thread; FND-12/FND-13 move this |

### Events

```cpp
SceneLoadedEvent evt;
evt.LoadedScene = CurrentScene;
evt.Fire();     // now, on this (main) thread
evt.Queue();    // copied; delivered by FirePendingEvents at the start of the next frame

class Thing : public EventReceiver {
    Thing() {
        Listen<SceneLoadedEvent>( [this]( const SceneLoadedEvent& evt ) { OnLoaded( evt ); } );  // void: never consumes
        Listen<PlayAudioEvent>( [this]( const PlayAudioEvent& evt ) { return Play( evt ); } );   // bool: true consumes
    }
};   // destructor deregisters automatically

// Lambda form, alive as long as the returned object:
std::unique_ptr<EventSubscription> sub = EventSubscription::Create<SceneLoadedEvent>(
    []( const SceneLoadedEvent& evt ) { ... } );
```

- `Listen<T>( handler )` registers the receiver for `T`. The handler takes `const T&`; the cast from `BaseEvent` happens once, inside the template, so handlers never switch on `GetEventId()`. A handler returning `bool` consumes the event by returning `true` (receivers after it don't see it); a `void` handler never consumes. Anything else fails to compile.
- One receiver may listen to several types, and to one type more than once: its handlers for a type run in `Listen` order until one consumes.
- Across receivers, dispatch order is the order in which they first listened to that type.
- Receivers may listen, deregister or be destroyed **during** dispatch: removed receivers are skipped; a receiver listening to a type for the first time is appended and also sees the current event; a handler added to a receiver that is already dispatching waits for the next event; a handler may destroy its own receiver (its remaining handlers are skipped).
- There is no per-type unsubscribe: destroy the receiver (or the `EventSubscription`) to stop listening.
- `Fire` asserts it's called on the thread that created the `EventManager` (the main thread).
- Events queued while `FirePendingEvents` runs are delivered the following frame.

Event inventory:

| Header | Events |
|--------|--------|
| `Source/Events/SceneEvents.h` | `LoadSceneEvent`, `SceneLoadedEvent`, `SaveSceneEvent`, `NewSceneEvent`, `AdditiveSceneEvent` |
| `Source/Events/PlatformEvents.h` | `WindowResizedEvent`, `WindowMovedEvent` |
| `Source/Events/AudioEvents.h` | `PlayAudioEvent`, `StopAudioEvent` |
| `Source/Engine/Input.h`, `Source/Input/Gamepads.h`, `Source/Input/InputPlayers.h` | `KeyPressEvent`, `MouseScrollEvent`, `GamepadConnectionEvent`, `InputPlayerEvent` |
| `Source/Physics/PhysicsTypes.h` | `CollisionEvent` (3D and 2D contacts and triggers) |
| `Source/Components/Animation/Animator.h` | `AnimationEvent` |
| `Source/Cores/NetCore.h` | `NetSessionEvent` (who joined and left a network session; `Source` = the world) |
| `Source/Events/HavanaEvents.h` (editor) | `InspectEvent`, `ClearInspectEvent`, `ModelHierarchyEvent` |
| `Modules/Dementia/Source/Events/EditorEvents.h` (editor) | `PickingEvent`, `PreviewResourceEvent`, `RequestAssetSelectionEvent`, `AssetsChangedEvent` |
| `Modules/Havana/Source/Editor/Selection.h` (editor) | `SelectionChangedEvent` |

## The Threading Rulebook

Safe inside a job:

- Pure computation over the job's own slice of a pre-sized container.
- Reading components and **already resolved** transform matrices (`Transform::UpdateAll` runs before `RenderCore`'s jobs; jobs never trigger lazy matrix recomputation there).
- `CommandCache` operations (`Modules/Moonlight/Source/Utils/CommandCache.h`) — internally mutexed.
- `CLog` (mutex-guarded), `Event<T>::Queue`, `ResourceCache::Get` (thread safe, but see below about GPU resources), nested `ParallelFor`/`Wait`.

**Not** safe inside a job (main thread only):

- `Event<T>::Fire` and receiver (de)registration.
- Entity/component create, destroy, add/remove, activation — the World is single-threaded.
- Writing transforms whose subtrees other jobs touch (marking dirty walks the children).
- bgfx calls and loading GPU-backed resources (textures, shaders, models create bgfx objects).
- ImGui, config, scripting calls.

If a job needs any of the above, record the intent in the job's own slice of a results buffer and apply it on the main thread after the wait.

## Caveats & Fragility

- **No exception isolation**: a throwing job terminates the process (the crash handler reports it).
- **Idle workers spin briefly then sleep** (up to 5 ms wake-up latency for the first job after a long idle period).
- **Queued events are copied** — event types used with `Queue()` must be copyable.
- `SceneGraphTestCore` (game) rotates transforms inside jobs; parents and children can be in different batches, so their dirty flags are written concurrently. It works in practice but isn't a pattern to copy.

## Related Docs

- `Docs/Architecture.md` — where `FirePendingEvents`, fixed steps and job-consuming updates sit in the frame
- `Docs/Rendering-Pipeline.md` — the mesh job's producer/consumer relationship with the renderer
- `Docs/ECS.md` — `TypeId` generation shared by events and components
- `Docs/State-of-the-Engine.md` — assessment and priorities
