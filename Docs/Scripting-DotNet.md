# Scripting (.NET 8)

Game scripts are C# classes implementing `IGameScript` (usually through the `Script` base class), hosted in-process via **hostfxr** (.NET 8). C++ loads `[UnmanagedCallersOnly]` bridge functions from `ScriptCore.dll`; C# gets a struct of engine callbacks (`ScriptEngineAPI`) that is **generated from one manifest** for both languages. Scripts live in a collectible `AssemblyLoadContext`, and editing a `.cs` file in a tools build **hot reloads** them with their field values kept. This doc covers the build, the bootstrap chain, the script lifecycle, hot reload, the API surface, platform status and the add-a-binding recipe.

> Verified against engine commit 6a4b006f, 2026-10-09; the navigation API against 8d6769a5, 2026-10-10; collision callbacks and field access against the commit that added them, 2026-10-10.

## Overview

The feature-flag chain is `DEFINE_ME_DOTNET` (set by Sharpmake **only when the .NET host directory exists**; see `Docs/Build-System.md`) → `ME_DOTNET` → `ME_SCRIPTING`.

The engine owns scripting the way it owns physics and audio: `Engine::Scripts` is a `ScriptCore` created at startup, added to every world (not saved into scenes), and ticked from the main loop. Its constructor calls `ScriptEngine::Init()`. That builds the scripts if needed, starts .NET, loads `ScriptCore.dll` and `Game.Script.dll` from the executable's directory, and fills the API table.

## Key Files

| Path | Role |
|------|------|
| `Source/Scripting/ScriptHost.h` / `.cpp` | hostfxr wrapper: init runtime, `LoadFunction("Namespace.Class, Assembly", method)`, `Shutdown` |
| `Source/Scripting/ScriptEngine.h` / `.cpp` | Static façade: CLI build, init, hot reload, `CreateScript`, per-frame calls, fields JSON |
| `Source/Scripting/ScriptAPI.def` | **The manifest**: every engine function C# can call, grouped by domain, in ABI order |
| `Tools/GenerateScriptAPI.py` | Generates both ABI halves from the manifest; `--check` fails when they are stale |
| `Source/Scripting/Generated/ScriptEngineAPI.generated.h` | Generated C++ `struct ScriptEngineAPI` (+ `ScriptRaycastHit`) |
| `Modules/ScriptCore/Source/Generated/EngineAPIBindings.g.cs` | Generated C# `struct EngineAPIBindings` (`delegate* unmanaged<>` members) |
| `Source/Scripting/Bindings/` (`Entity`, `ImGui`, `Components/`, `Systems/`) | `Register_*Bindings( ScriptEngineAPI& )` implementations; `Systems/Gameplay.bindings.cpp` holds time, lifecycle, world-space transforms, scenes, input actions, audio, physics and debug draw |
| `Source/Scripting/Bindings/BindingContext.h` / `.cpp` | Script-world pointer + name-keyed component ops (`RegisterComponent<T>`) |
| `Source/Scripting/ScriptCore.runtimeconfig.json` | Runtime config (net8.0) handed to hostfxr (also looked up next to the executable and in the working directory) |
| `Source/Cores/Scripting/ScriptCore.cpp` | The engine-owned core: start, fixed update, update, destroy |
| `Source/Components/Scripting/ScriptComponent.h` / `.cpp` | Script name, handle, started flag, saved-fields JSON, inspector |
| `Modules/ScriptCore/ScriptCore.csproj` | SDK-style project for command-line builds |
| `Modules/ScriptCore/ScriptCore.sharpmake.cs` | Visual Studio project for `ScriptCore.dll` (Windows) |
| `Modules/ScriptCore/Source/` | The managed side (below) |
| Game repo `Project/Game.Script.csproj` | Compiles every `.cs` under `Assets/` into `Game.Script.dll`, referencing `ScriptCore.csproj` |

The managed side has these parts:

- **`IGameScript.cs`** declares `OnStart`, `OnUpdate(float)`, `OnDestroy` and `OnEditorInspect`, plus default `OnFixedUpdate(float)` and `OnReload()`.
- **`Script.cs`** is the base class with `Entity`, `transform` and the `Get/Has/AddComponent<T>` helpers.
- **`Engine.cs`** is the `ScriptBridge` entry points and the instance table, and contains the hot reload logic.
- **`GameScriptALC.cs`** is the collectible ALC. It redirects `ScriptCore` references to the already-loaded assembly.
- **`ScriptRegistry.cs`** finds `IGameScript` implementors, keyed by `Type.Name`.
- **`Inspector.cs`** is the reflection-driven ImGui field editor and the JSON field serializer.
- **`Core/`** holds `Entity`, `World`, `Time`, `Input`, `Audio`, `Physics` and `Debug`.
- **`Components/`** holds `Transform`, `Rigidbody`, `Camera` and `BasicUIView`.

## How It Works

### Building scripts

On Windows, Sharpmake generates the `ScriptCore` and `UserGameScript` C# projects into the solution, and Visual Studio builds them.

Sharpmake can't generate C# projects elsewhere, so **the engine builds them itself**. `ScriptEngine::BuildScripts` runs:

```
dotnet build "Project/Game.Script.csproj" -c Debug|Release -o "<exe dir>" --artifacts-path .tmp/dotnet --nologo -v q
```

That builds `ScriptCore.csproj` through the project reference and drops both DLLs next to the executable. `bin/` and `obj/` stay under `.tmp/dotnet`, out of the tree.

Tools builds (editor, `ME_TOOLS`) run it at `Init` when `NeedsBuild()` finds `Game.Script.dll` missing or older than any `.cs`/`.csproj` under `Assets/`, `Engine/Modules/ScriptCore/Source` or the two project files. Game builds only load what is there, so ship the DLLs with the executable.

You can also build by hand from the game repo root with the same command.

### Bootstrap

```mermaid
sequenceDiagram
    participant E as Engine::Init
    participant SE as ScriptEngine::Init (C++)
    participant H as ScriptHost / hostfxr
    participant CS as ScriptCore.dll (C#, "ScriptBridge")
    participant G as Game.Script.dll
    E->>SE: new ScriptCore() → Init()
    SE->>SE: tools: NeedsBuild()? → dotnet build into the exe dir
    SE->>H: Init(runtimeconfig) (exe dir via SDL_GetBasePath)
    H-->>SE: load_assembly_and_get_function_pointer
    SE->>CS: LoadFunction ×14 (LoadGameAssembly, ReloadGameAssembly,<br/>SetEngineAPI, CreateScript, ScriptOn*, Get/SetFieldsJson,<br/>IsHandleAlive, GetScriptCount/Name)
    SE->>SE: fill ScriptEngineAPI (Register_*Bindings);<br/>a null slot aborts init
    SE->>CS: SetEngineAPI(&api, sizeof(api))
    Note over CS: size check — a mismatch aborts
    CS->>G: LoadGameAssembly → bytes into a new GameScriptALC<br/>→ ScriptRegistry.Scan
```

- The game assembly is **loaded from memory** (`LoadFromStream`), so the file on disk is never locked and a rebuild can overwrite it while the old version runs.
- `ScriptEngine::Shutdown` (from `Engine` shutdown) closes the hostfxr context explicitly. The host library is never unloaded: tearing it down from static destructors crashed with a double free.

### Script lifecycle

| When | What runs |
|------|-----------|
| Scene load / spawn (`ScriptComponent::Init`) | `CreateScript(name, entity)` returns a handle; saved field JSON is applied. **No `OnStart` yet** |
| Play / game start (`ScriptCore::OnStart`), and each later frame for newly spawned scripts | `PhysicsCore::SyncNow` + `Physics2DCore::SyncNow` (so bodies and colliders exist), then `OnStart` for every created, unstarted script on an active entity |
| Each fixed step, before physics steps | `OnFixedUpdate(fixedDt)` |
| Each frame, after the scene cores | `OnUpdate(dt)` (scaled; zero while paused) |
| Entity destroyed / scene unloaded | `OnDestroy`, handle released |
| Physics contact / trigger (3D and 2D) | `OnCollisionEnter` / `OnCollisionExit( Collision )` (other entity, point, normal from this entity towards the other) and `OnTriggerEnter` / `OnTriggerExit( Entity other )`, on the collider's entity and on the body it belongs to (compound colliders). `ScriptCore` receives `CollisionEvent` and calls both sides |
| Editor inspector | `OnEditorInspect` (fields drawn by `Inspector.cs`) |

Scripts run only while the game runs: in the editor that means Play mode, while the game executable runs them from its first frame. Disabled `ScriptComponent`s and inactive entities are skipped. Public fields round-trip as JSON (`GetFieldsJson`/`SetFieldsJson`, no size cap), which is how the inspector saves script data into scenes and how Play-mode snapshots restore it.

### Hot reload (tools builds)

1. The asset watcher sees a `.cs` change and calls `ScriptEngine::RequestReload()`. The inspector's **Rebuild && Reload Scripts** button on `ScriptCore` does the same. This starts `dotnet build` on a worker thread. A change during a build queues one more build.
2. `ScriptEngine::PollReload()` (main thread, every frame) picks up the result. A failed build logs the compiler output and leaves the running scripts untouched.
3. On success `ReloadGameAssembly` loads the new DLL into a fresh ALC and snapshots every live instance's fields as JSON. It then unloads the old ALC and re-creates each instance **under the same handle**, so C++ never notices, restores its fields, and calls `OnReload()`.
4. A class that no longer exists is dropped. `ScriptComponent::EnsureCreated` notices the dead handle (`IsHandleAlive`) and re-creates the script if the class comes back later.

Only public fields survive. Private state, statics and references are rebuilt in `OnReload`. The log line `Scripts: hot reloaded (N live script(s) kept their fields)` marks a finished swap, and `ScriptFlows.edscript` asserts on it.

### Calling the engine from C#

The API is about 87 functions. Strings cross as UTF-8 `byte*`, entities as the raw 64-bit `EntityID`, and vectors by pointer.

| Area | C# surface |
|------|------------|
| Logging | `Debug.Log/Warning/Error` |
| Debug draw | `Debug.DrawLine/DrawSphere(…, duration)` |
| Time | `Time.DeltaTime` (scaled), `UnscaledDeltaTime`, `FixedDeltaTime`, `TimeScale` (get/set), `RealTime` (wall clock) |
| Entity | `IsAlive`, `Name`, `Active`, `Destroy()`, `Has/Add/GetComponent<T>` |
| Transform | Local `Position`/`Rotation`/`Scale`; `WorldPosition`; `Forward`/`Right`/`Up`; `LookAt`; `Parent` |
| World | `CreateEntity`, `Find(name)` (any depth), `Instantiate(prefab, parent)`, `LoadScene(path)` (queued to the end of the frame) |
| Input | `IsKeyDown`, `WasKeyPressed`, `MouseDelta`, `GetAction`, `GetActionVector2`, `IsActionPressed`, `WasActionPressed`, `WasActionReleased` (actions from `Docs/Input.md`) |
| Audio | `Audio.PlayOneShot(clip, volume)`, `PlayOneShotAt(clip, position, volume)` |
| Physics | `Physics.Raycast(origin, dir, maxDistance, out RaycastHit)`; `Rigidbody.AddForce(force, ForceMode)` and `Velocity` |
| Character | `CharacterController.SetMoveInput`, `Move`, `Jump`, `IsGrounded`, `Velocity`, `MaxSpeed` (`Docs/Physics.md`) |
| Math | `Vector3` (`Zero`/`Up`/`Forward`…, `Normalized`, `Dot`, `Cross`, `Distance`, `Lerp`), `Mathf` (`Clamp`, `Atan2`, `Deg2Rad`, frame-rate independent `Damp`) |
| Any component | `Entity.GetField<T>( component, path, fallback )`, `SetField( component, path, value )` and the raw `GetFieldJson` / `SetFieldJson`: any reflected field by path (`"Light", "Intensity"`, `"Color.0"`), or any key of a hand-serialized component's JSON. Vectors are `[x, y, z]` arrays and enums are names. Setting calls the component's `OnPropertyChanged` |
| Navigation | `NavMeshAgent.SetDestination`, `IsStopped`, `ResetPath`, `Warp`, `HasPath`, `HasArrived`, `RemainingDistance`, `Velocity`, `DesiredVelocity`, `Speed`; `Navigation.FindPath`, `SamplePosition`, `Raycast`, `GetRandomPoint` (`Docs/Navigation.md`) |
| Camera / UI | Clear colour; `BasicUIView.ExecuteJS` |
| ImGui | A subset for `OnEditorInspect` |

### Platform status

| Platform | Status |
|----------|--------|
| Win64 | `DEFINE_ME_DOTNET` when `Globals.DOTNET_Win64_Dir` (the `Microsoft.NETCore.App.Host.win-x64` native pack) exists. `nethost` is copied to the output, and the Sharpmake C# projects are in the solution. The CLI build and hot reload paths are written but untested there |
| Linux | ✅ Verified. Needs `Globals.DOTNET_Linux_Dir`. The Nix shell (game repo `flake.nix`) exports `DOTNET_ROOT` and finds the host pack's `runtimes/linux-x64/native` directory. Executables link `libnethost.so` with an rpath to it (`Tools/BaseProject.sharpmake.cs`), and the scripts build with the CLI |
| macOS | ❌ No `DEFINE_ME_DOTNET` path yet, so scripting compiles out. The CLI build would carry over; the host pack lookup and link are what's missing |
| UWP | ❌ Not wired |

## How to Extend

### Add a script binding (engine function callable from C#)

1. **Declare** the function in `Source/Scripting/ScriptAPI.def` by **appending** it under a `## Section` (order is the ABI):
   ```
   void Transform_LookAt( Entity id, const Vector3* target )
   ```
   The types are `void bool int float cstr Entity`, plus pointers to `Entity`, `Vector2`, `Vector3`, `float`, `int`, `bool`, `byte` and `RaycastHit`, and `const Vector3*`. `bool` crosses as a byte.
2. **Generate**: `python3 Engine/Tools/GenerateScriptAPI.py` rewrites both generated files. CI-style check: `--check`.
3. **Implement and register** in the matching binding file. Write a static `Eng_` function and assign it in that file's `Register_*Bindings( ScriptEngineAPI& )`. If you forget, `Init` logs the unfilled slot's index and leaves scripting off, rather than letting a C# call jump to null.
4. **Wrap** it for script authors in `Modules/ScriptCore/Source` (for example `Transform.LookAt`), calling `Engine._api.Transform_LookAt(...)`.

For a new *component* reachable from `Entity.Has/AddComponent`, call `ScriptBindings::RegisterComponent<MyComponent>( "MyComponent" )` during binding registration, and add a matching `Component` subclass in C#.

### Add a game script

```csharp
using ScriptCore;

public class Spinner : Script
{
    public float Speed = 90f;          // public fields: inspector, scene file, survive hot reload

    public override void OnUpdate(float deltaTime)
    {
        var rotation = transform.Rotation;
        transform.Rotation = new Vector3(rotation.x, rotation.y + Speed * deltaTime, rotation.z);
        if (Input.WasActionPressed("Jump"))
        {
            Audio.PlayOneShot("Assets/Sounds/Jump.wav");
        }
    }
}
```

Save it anywhere under `Assets/`. The editor builds it on the next start, or hot reloads it if already running. Then add a `ScriptComponent` naming `Spinner` to an entity. `Assets/Scripts/Tests/ScriptProbe.cs` and `Assets/Scenes/Tests/ScriptTest.lvl` are a working example that `ScriptFlows.edscript` drives headlessly.

## Caveats & Fragility

- **The ABI guard is `sizeof` plus the generator.** The struct is append-only. Never hand-edit the generated files; `--check` catches drift between the manifest and the outputs.
- **Script lookup is `Type.Name`**: two scripts with the same class name in different namespaces clash.
- **Handles are bare ints.** They stay stable across reloads, and `IsHandleAlive` validates them, but there is no generation counter.
- **Game builds don't compile scripts.** The DLLs must already sit next to the executable, or scripting logs a warning and stays off.
- **The .NET SDK must be on `PATH`** for tools builds to compile. Without it the existing DLLs are used as-is.
- **All script calls are synchronous on the main thread.** A slow `OnUpdate` is a frame hitch; there is no sandboxing or time budget.
- **Field access by name goes through JSON.** It reaches every component, but a typed wrapper (like `Rigidbody`) is faster for per-frame use. Wrong names return the fallback or `false`, not an error.

## Related Docs

- `Docs/Build-System.md`: `DOTNET_*_Dir` detection, nethost linking, flake.nix
- `Docs/Architecture.md`: where scripts sit in the frame
- `Docs/Editor-Havana.md`: the inspector, `--editor-exec` (`wait-log`, `replace-in-file`)
- `Docs/Input.md`: the action names scripts read
- `Docs/Physics.md`: `SyncNow`, raycasts, rigidbody forces
