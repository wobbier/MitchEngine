# Jobs and Events

The engine's two cross-cutting runtime services: `Jobs::JobSystem`, a work-stealing job system with counters and `ParallelFor`, and `EventManager`, a typed publish/subscribe bus with immediate and queued delivery. This doc is the **threading rulebook** — what may run on a job and what must stay on the main thread.

> Verified against engine commit dab803a2, 2026-10-08.

## Overview

- `Jobs::JobSystem::Get()` starts `hardware_concurrency - 1` workers on first use. Every thread that isn't a worker shares queue 0; workers pop their own queue newest-first and steal oldest-first from the others, then sleep when idle.
- Waiting on a `Jobs::Counter` *helps* — the waiting thread executes queued jobs — so nested parallelism (waiting inside a job) cannot deadlock.
- Events: `Fire()` dispatches immediately on the main thread; `Queue()` copies the event and delivers it at the start of the next frame and is safe from any thread.

## Key Files

| Path | Role |
|------|------|
| `Modules/Dementia/Source/Jobs/JobSystem.h` / `Modules/Dementia/Source/Jobs/JobSystem.cpp` | `Jobs::JobSystem`, `Jobs::Counter`, `ParallelFor`, `Submit` |
| `Modules/Dementia/Source/Work/Burst.h` | `Burst::GenerateChunks` range helper (legacy; still used by the Bullet `PhysicsCore`) |
| `Modules/Dementia/Source/Events/Event.h` | `Event<T>` CRTP: `GetEventId`, `Fire`, `Queue` |
| `Modules/Dementia/Source/Events/EventManager.h` / `Modules/Dementia/Source/Events/EventManager.cpp` | Receiver registry, dispatch, the cross-thread queue |
| `Modules/Dementia/Source/Events/EventReceiver.h` / `Modules/Dementia/Source/Events/EventReceiver.cpp` | `EventReceiver` (auto-deregistering), `EventSubscription` (lambda) |
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
    Thing() { EventManager::GetInstance().RegisterReceiver( this, { SceneLoadedEvent::GetEventId() } ); }
    bool OnEvent( const BaseEvent& evt ) override {
        // return true to consume (receivers registered later won't see it)
        return false;
    }
};   // destructor deregisters automatically

// Lambda form, alive as long as the returned object:
std::unique_ptr<EventSubscription> sub = EventSubscription::Create<SceneLoadedEvent>(
    []( const SceneLoadedEvent& e ) { return false; } );
```

- Dispatch order is registration order; duplicate registrations are ignored.
- Receivers may register, deregister or be destroyed **during** dispatch: removed receivers are skipped, new ones are appended and also see the current event.
- `Fire` asserts it's called on the thread that created the `EventManager` (the main thread).
- Events queued while `FirePendingEvents` runs are delivered the following frame.

Event inventory: `Source/Events/SceneEvents.h` (`LoadSceneEvent`, `SceneLoadedEvent`, `SaveSceneEvent`, `NewSceneEvent`), `Source/Events/PlatformEvents.h` (`WindowResizedEvent`, `WindowMovedEvent`), `Source/Events/AudioEvents.h` (`PlayAudioEvent`, `StopAudioEvent`), `Source/Events/HavanaEvents.h` (editor: `PickingEvent`, `PreviewResourceEvent`, `RequestAssetSelectionEvent`, `InspectEvent`), `Modules/Dementia/Source/Events/EditorEvents.h` (`ClearInspectEvent`, `TestEditorEvent`).

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
