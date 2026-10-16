# Jobs and Events

The engine's two cross-cutting runtime services: `Jobs::JobSystem`, a work-stealing job system with counters and `ParallelFor`, and `EventManager`, a typed publish/subscribe bus with immediate and queued delivery. This doc is the **threading rulebook** — what may run on a job and what must stay on the main thread.

> Verified against engine commit dab803a2, 2026-10-08; the physics task bridge against bdfedae8, 2026-10-09; typed `Listen` receivers against 07595696 plus the typed-events change, 2026-10-16.

## Overview

- `Jobs::JobSystem::Get()` starts `hardware_concurrency - 1` workers on first use. Every thread that isn't a worker shares queue 0; workers pop their own queue newest-first and steal oldest-first from the others, then sleep when idle.
- Waiting on a `Jobs::Counter` *helps* — the waiting thread executes queued jobs — so nested parallelism (waiting inside a job) cannot deadlock.
- Events: `Fire()` dispatches immediately on the main thread; `Queue()` copies the event and delivers it at the start of the next frame and is safe from any thread.

## Key Files

| Path | Role |
|------|------|
| `Modules/Dementia/Source/Jobs/JobSystem.h` / `Modules/Dementia/Source/Jobs/JobSystem.cpp` | `Jobs::JobSystem`, `Jobs::Counter`, `ParallelFor`, `Submit` |
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
