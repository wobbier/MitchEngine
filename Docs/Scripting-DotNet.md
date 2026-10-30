# Scripting (.NET 8)

Game scripts are C# classes implementing `IGameScript` (usually through the `Script` base class), hosted in-process via **hostfxr** (.NET 8). C++ loads `[UnmanagedCallersOnly]` bridge functions from `ScriptCore.dll`; C# gets a struct of engine callbacks (`ScriptEngineAPI`) that is **generated from one manifest** for both languages. Scripts live in a collectible `AssemblyLoadContext`, and editing a `.cs` file in a tools build **hot reloads** them with their field values kept. This doc covers the build, the bootstrap chain, the script lifecycle, hot reload, the API surface, platform status and the add-a-binding recipe.

> Verified against engine commit 6a4b006f, 2026-10-09; the navigation API against 8d6769a5, 2026-10-10; collision callbacks and field access against f4ed921c, 2026-10-10; `Camera.TargetTexture` / `UpdateInterval` against 89e036be plus the render texture change, 2026-10-29.

## Overview

The feature-flag chain is `DEFINE_ME_DOTNET` (set by Sharpmake **only when the .NET host directory exists**; see `Docs/Build-System.md`) → `ME_DOTNET` → `ME_SCRIPTING`.

The engine owns scripting the way it owns physics and audio: `Engine::Scripts` is a `ScriptCore` created at startup, added to every world (not saved into scenes), and ticked from the main loop. Its constructor calls `ScriptEngine::Init()`. That builds the scripts if needed, starts .NET, loads `ScriptCore.dll` and `Game.Script.dll` from `ScriptEngine::GetAssemblyDirectory()` (next to the executable, or inside the project for a shared editor or Player, see below), and fills the API table.

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
| `Source/Scripting/ScriptCore.runtimeconfig.json` | Runtime config (net8.0) handed to hostfxr: looked up beside the assemblies first (`ScriptCore.csproj` copies it into the script build's output, so a cooked tree runs scripts from any directory), then next to the executable, then the working directory, then this path in the engine |
| `Source/Cores/Scripting/ScriptCore.cpp` | The engine-owned core: start, fixed update, update, destroy |
| `Source/Components/Scripting/ScriptComponent.h` / `.cpp` | Script name, handle, started flag, saved-fields JSON, inspector |
| `Modules/ScriptCore/ScriptCore.csproj` | SDK-style project for command-line builds |
| `Modules/ScriptCore/ScriptCore.sharpmake.cs` | Visual Studio project for `ScriptCore.dll` (Windows) |
| `Modules/ScriptCore/Source/` | The managed side (below) |
| Game repo `Project/Game.Script.csproj` | Compiles every `.cs` under `Assets/` into `Game.Script.dll`, referencing `ScriptCore.csproj` |

The managed side has these parts:

- **`IGameScript.cs`** declares `OnStart`, `OnUpdate(float)`, `OnDestroy` and `OnEditorInspect`, plus default `OnFixedUpdate(float)`, `OnLateUpdate(float)` and `OnReload()`.
- **`Script.cs`** is the base class with `Entity`, `transform` and the `Get/Has/AddComponent<T>` helpers.
- **`Engine.cs`** is the `ScriptBridge` entry points and the instance table, and contains the hot reload logic.
- **`GameScriptALC.cs`** is the collectible ALC. It redirects `ScriptCore` references to the already-loaded assembly.
- **`ScriptRegistry.cs`** finds `IGameScript` implementors, keyed by `Type.Name` (`Find` also matches a `[FormerName]`), and declares `FormerNameAttribute` and `ExecutionOrderAttribute`.
- **`Inspector.cs`** is the reflection-driven field editor and the JSON field serializer. It draws every public field as the same Havana property rows the C++ reflection inspector uses: `bool`/`int`/`float`/`double` (with `[Range]` sliders), `string` (`[Multiline]` for a box), enums as combos, `Vector2/3/4`, `Quaternion` (xyzw, normalized on edit), `Color` (color row), `List<T>`/arrays (tree with +/- buttons), nested `[Serializable]` structs and classes (tree, recursed), `EntityRef` (entity row: drag from the hierarchy, click to select, x to clear; stores the scene GUID so it survives save/load) and `AssetRef` (asset row with browser drag-drop and picker; stores the project path, `[AssetFilter("Texture")]` narrows the picker). Layout attributes: `[Header]`, `[Space]`, `[Tooltip]`, `[ReadOnly]` (greyed out), `[HideInInspector]`. The rows cross the ABI as `ImGui_*` slots; the editor installs its drag-drop-capable entity/asset rows through `Scripting/ScriptInspectorRows.h` (plain fallbacks elsewhere). `Assets/Scenes/Tests/InspectorFlows.edscript` edits each kind through `script-set`, saves, reloads and asserts, then resolves the `EntityRef` in Play.
- **`Core/`** holds `Entity`, `World`, `Time`, `Input`, `Audio`, `Physics`, `Debug` and `UI` (RmlUi view models: `UI.Model("hud")` → `Set`/`On`/`GetNumber`/`GetString`/`PollEvent`; values live engine-side so hot reloads keep them — see `UI-RmlUi-and-ImGui.md`).
- **`Components/`** holds `Transform`, `Rigidbody`, `CharacterController`, `NavMeshAgent`, `Camera` and `Mesh` (`BasicUIView` left with Ultralight in UI-5; its `ExecuteJS` ABI slot is a stub that warns once).

## How It Works

### Building scripts

On Windows, Sharpmake generates the `ScriptCore` and `UserGameScript` C# projects into the solution, and Visual Studio builds them.

Sharpmake can't generate C# projects elsewhere, so **the engine builds them itself**. `ScriptEngine::BuildScripts` runs:

```
dotnet build "Project/Game.Script.csproj" -c Debug|Release -o "<assembly dir>" -p:MitchEngineDir="<engine>/" --artifacts-path .tmp/dotnet --nologo -v q
```

That builds `ScriptCore.csproj` through the project reference and drops both DLLs in the assembly directory. `bin/` and `obj/` stay under `.tmp/dotnet`, out of the tree. `MitchEngineDir` is where the script project finds `ScriptCore.csproj` and the generator targets; it defaults to the project's `Engine/`, and the engine always passes its own root (`Project::GetEngineRoot`).

The **assembly directory** (`ScriptEngine::GetAssemblyDirectory`) is next to the executable, which is where a project's own builds (`<project>/.build/<config>/`) and shipped builds keep them. A *shared* executable, one built outside the project (another checkout's editor or the Player running a C#-only project, `Docs/Projects.md`), builds into `<project>/.tmp/Scripts/<Executable>-<Debug|Release>/` instead, one folder per executable so the editor and the Player can run the project at once. That folder's `Engine.stamp` names the engine it was built against, and another engine rebuilds it. When the engine isn't the project's `Engine/`, the build also writes `.tmp/MitchEngine.props`, which the script project imports so IDEs resolve `ScriptCore` too.

Tools builds (editor, `ME_TOOLS`) run it at `Init` when `NeedsBuild()` finds `Game.Script.dll` missing or older than any `.cs`/`.csproj` under `Assets/`, the engine's `Modules/ScriptCore/Source` or the two project files. Game builds only load what is there, so ship the DLLs with the executable.

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
    SE->>SE: tools: NeedsBuild()? → dotnet build into the assembly dir
    SE->>H: Init(runtimeconfig) (found beside the assemblies)
    H-->>SE: load_assembly_and_get_function_pointer
    SE->>CS: LoadFunction(GetBridge), then GetBridge(&bridge, size, hash)<br/>fills all 32 entry points (ScriptBridge.def, ME-299)
    SE->>SE: fill ScriptEngineAPI (Register_*Bindings);<br/>a null slot aborts init
    SE->>CS: SetEngineAPI(&api, sizeof(api))
    Note over CS: size check — a mismatch aborts
    CS->>G: LoadGameAssembly → bytes into a new GameScriptALC<br/>→ ScriptRegistry.Scan
```

- The game assembly is **loaded from memory** (`LoadFromStream`), so the file on disk is never locked and a rebuild can overwrite it while the old version runs.
- `ScriptEngine::Shutdown` (from `Engine` shutdown) closes the hostfxr context explicitly. The host library is never unloaded: tearing it down from static destructors crashed with a double free.
- **Which runtime**: a `dotnet/` folder next to the executable is an **app-local install** (`host/fxr/<version>/` and `shared/Microsoft.NETCore.App/<version>/`, copied there by Build Game, `Docs/Building.md`). When it exists, `ScriptHost::Init` hands it to `get_hostfxr_path` and `hostfxr_initialize_for_runtime_config` as `dotnet_root`, so players need no .NET install. Otherwise the system install is used (`DOTNET_ROOT`, then the OS default locations). Either way the log says which at Info level (`Scripts: .NET runtime from the app-local install (...)`). Without either, `get_hostfxr_path` fails with `0x80008083` and scripting stays off.

### Script lifecycle

| When | What runs |
|------|-----------|
| Scene load / spawn (`ScriptComponent::Init`) | `CreateScript(name, entity)` returns a handle; saved field JSON is applied. **No `OnStart` yet** |
| Play / game start (`ScriptCore::OnStart`), and each later frame for newly spawned scripts | `PhysicsCore::SyncNow` + `Physics2DCore::SyncNow` (so bodies and colliders exist), then `OnStart` for every created, unstarted script on an active entity |
| Each fixed step, before physics steps | `OnFixedUpdate(fixedDt)`. Input edges here (`Input.WasActionPressed`, `WasKeyPressed`) are per step, so each press is seen exactly once at any frame rate (`Docs/Input.md`). In a network session this is simulation: networked entities' scripts run it on the server and, on a client, only for its predicted pawn, also in resimulation (`Net.IsResimulating`; see Networking.md, NET-6) |
| Each frame, after the scene cores | `OnUpdate(dt)` (scaled; zero while paused) |
| Each frame, after navigation and animation, before audio and rendering | `OnLateUpdate(dt)`: follow cameras, IK targets, props on bones. A script started mid-frame gets its first one after its first `OnUpdate` |

**Order.** Scripts run by `[ExecutionOrder(n)]` (lower first, default 0) and then by the order they started, in `OnStart` and in every update. Without the attribute, that's the order they joined the scene. The engine only calls the update callbacks a class implements: `ScriptGetSchedule` reports them per class, which saves a native→managed call per script per frame for the ones it doesn't.
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

**Unload check (ME-302):** after each hot reload, the old script context is garbage-collected a few times (after later reloads, and frames apart during Play). The log says "A previous Game.Script assembly unloaded". If something still holds it (a static, an event handler on an engine or ScriptCore object, a running Task or timer), a warning says so once, because each reload would then leak another copy.

**Exceptions during Play (ME-234):**
- **Repeats are collapsed:** the same exception from the same callback is logged the 1st, 10th, 100th... time, with a count, rather than every frame. Counts reset on Stop and on hot reload.
- **Pause on script exception:** with Preferences > General > Pause on script exception on, the first callback to throw (OnStart/OnUpdate/OnFixedUpdate/OnLateUpdate or a collision callback) pauses Play and selects that script's entity (`ScriptBridge.TakeScriptFault`, polled by the editor each frame).

**Errors.** A failed build logs each compiler diagnostic as its own entry, `Assets/File.cs:line:col: error CS1002: ; expected` (warnings at warning level; `ScriptEngine::ParseBuildDiagnostics`), then a summary line; the Log panel opens the line on double-click. They also stay in the **Error List** panel (Window > Error List, `ScriptEngine::GetBuildDiagnostics`; double-click opens the line) until a build succeeds, and the menu bar shows a red "N C# errors" button that opens it (ME-233). While the last build failed (`ScriptEngine::HasBuildErrors`), Play is refused with a message, since the last good scripts would run instead. An exception thrown by a script callback is logged at error level with the whole exception, its stack included, down to the throwing `File.cs:line N` when symbols loaded (Debug builds). `ScriptFlows.edscript` covers both.

### IDE integration

- **Open at file:line.** Double-clicking a `.cs` in the asset browser, or a `File.cs:line` reference in the Log panel, opens the configured code editor: the `CodeEditorCommand` preference (Preferences → "Code editor"), else `$ME_CODE_EDITOR`, else VS Code (`code -g "{file}:{line}"`). `{file}` and `{line}` are substituted.
- **Open C# Project** (right-click any `.cs`): opens the generated game solution when one exists (Windows — it carries the C# projects); otherwise it generates a minimal `Project/Scripts.sln` (Game.Script + ScriptCore, gitignored) and opens the project root in the code editor, where VS Code's C# Dev Kit discovers the solution. The game repo recommends `ms-dotnettools.csharp` + `csdevkit` in `.vscode/extensions.json`.
- **Attach the .NET debugger** (the game repo ships the launch config):
  1. Run Havana (F5 "Havana (Editor_Debug, Linux)" for mixed sessions, or launch it outside VS Code).
  2. Run the **"Attach to Havana (.NET)"** launch config and pick the `Havana` process. The C# debugger (`coreclr`) attaches alongside any native debugger.
  3. Set a breakpoint in a game script (e.g. `OnUpdate`) and press Play — it binds and hits. Game assemblies load from memory, but their portable PDBs load as streams with them (`Engine.cs` `LoadFromStream(image, symbols)`), so sources resolve.
  4. Edit the `.cs`, let hot reload swap the ALC, press Play again: the breakpoint re-binds in the newly loaded assembly and hits again — each reload is a fresh module load to the debugger.
  - Breakpoints inside `OnReload` itself are the one blind spot: the debugger may still be binding the new module while it runs.

### Calling the engine from C#

**Strings into the engine (ME-300):** wrappers pass strings as `fixed (byte* p = Engine.Utf8(s, stackalloc byte[Engine.Utf8StackSize]))`. The string is encoded NUL-terminated into the caller's stack, so a per-frame `Animator.SetFloat("Speed", v)` makes no garbage. Only strings over 256 UTF-8 bytes fall back to an array. Use the same form in new wrappers; the one-argument `Engine.Utf8(s)` allocates.

The API is 120 functions (`ScriptAPI.def`). Strings cross as UTF-8 `byte*`, entities as the raw 64-bit `EntityID`, and vectors by pointer.

| Area | C# surface |
|------|------------|
| Logging | `Debug.Log/Warning/Error` |
| Debug draw | `Debug.DrawLine/DrawSphere(…, duration)` |
| Time | `Time.DeltaTime` (scaled), `UnscaledDeltaTime`, `FixedDeltaTime`, `TimeScale` (get/set), `RealTime` (wall clock) |
| Entity | `IsAlive`, `Name`, `Active`, `Destroy()`, `Has/Add/GetComponent<T>`, and `HasComponent(name)` / `AddComponent(name)` for any registered component (the binding registry falls back to the engine's component registry); `Children`, `Guid`, `Layer`. `Query.WithComponent(name)` / `Query.Named(name)` return every match |
| Transform | Local `Position`/`Rotation`/`Scale`; `WorldPosition`; `Forward`/`Right`/`Up`; `LookAt`, `LookDirection(forward, up)`; `Parent` |
| World | `CreateEntity`, `Find(name)` (any depth), `Instantiate(prefab, parent)`, `LoadScene(path)` (queued to the end of the frame), `LoadSceneAdditive(path, async = true)` / `UnloadScene(id)` / `GetSceneState(id)` (`Docs/Serialization-and-Scenes.md`) |
| Input | `IsKeyDown`, `WasKeyPressed`, `MouseDelta`, `GetAction`, `GetActionVector2`, `IsActionPressed`, `WasActionPressed`, `WasActionReleased` (actions from `Docs/Input.md`); local multiplayer: `PlayerCount`, `Player(n)` (a `PlayerInput` with the same action methods and its `Gamepad`), `SetJoining(maxPlayers)` |
| Audio | `Audio.PlayOneShot(clip, volume)`, `PlayOneShotAt(clip, position, volume)`; `AudioSource.Play/Stop/Pause/IsPlaying` |
| Particles, Animator | `ParticleSystem.Play/Stop/Burst(count)/ParticleCount`; `Animator.Play(state, fade)`, `Stop`, `IsPlaying`, `Set/GetFloat`, `Set/GetBool`, `SetTrigger`, `GetState(layer)`, `GetNormalizedTime`, `SetLayerWeight` (markers: the `AnimationMarker` event) |
| Sprites | `SpriteRenderer.Sprite` (a name in its sheet), `SetSprite(sheet, sprite)`, `Texture`, `FlipX` / `FlipY`, `Color` (sRGB tint + opacity), `SortingLayer`, `OrderInLayer` (ME-276; an Animator with flipbook clips sets the sprite while it plays) |
| Tilemaps | `Tilemap.GetTile(x, y)` / `SetTile(x, y, index)` / `ClearTile` / `FillRect(x0, y0, x1, y1, index)` / `Clear()` / `TileCount` / `WorldToCell(world)` (LOOK-S5; palette indices, -1 = empty; a TilemapCollider2D follows on the next step) |
| World text | `TextRenderer.Text` (get / set), `TextRenderer.Color` (sRGB + opacity) (LOOK-S12) |
| Cameras, lights, materials | `Camera.FieldOfView`, `Near`, `Far`, `OrthographicSize`, `OutputSize`, `ScreenToRay(screen, out origin, out direction)`, `WorldToScreen(world)` (pixels from the top left; z = distance in front); `Input.MousePosition` (game pixels); `Light.Color/Intensity/Range/Inner/OuterConeAngle`; `Mesh.Get/SetMaterial(field)` on the mesh's own material. Any other field of any component (PostProcess, Decal, joints...) through `Entity.GetField / SetField(component, "Path.To.Field")` |
| App | `App.Quit()` (ignored in the editor), `App.WindowSize`, `App.WindowMode`, `App.Rumble(player, low, high, seconds)` |
| Math | `Quaternion.Euler/ToEuler` (the engine's glm convention), `AngleAxis`, `LookRotation`, `Slerp`, `Angle`, `Inverse`, `q * q`, `q * v`; `Matrix4x4.TRS/Translate/Rotate/Scale`, `MultiplyPoint/Vector`, `Inverse`; `Color.FromHex/ToHex/FromHsv/Lerp` (sRGB↔linear); `Vector4.Dot/Lerp/Normalized`; `Rng` (PCG32, seedable: `Range`, `Chance`, `OnUnitSphere`, `Shuffle`). Tested in `Generators.Tests/MathTests.cs` |
| Data assets | `DataAsset` subclasses in `.asset` files; `AssetRef.Load<T>()` / `DataAssets.Load<T>(path)` (cached, shared), `DataAssets.Save` (see below) |
| Async | `await Async.Seconds(s)` / `Frames(n)` / `NextFrame()` / `Until(cond)`, `Async.Frame`; coroutines `StartCoroutine(IEnumerator)` with `WaitForSeconds` / `WaitFrames` / `WaitUntil` / `null`; `After(s, a)` / `Every(s, a)` (`Timer`); `Tween.Float` / `Vector` / `Move` with `Ease` (see below) |
| Events | `EventBus.Publish(evt)` / `Subscribe<T>(handler)` (a disposable token) / `Unsubscribe`, `Script.On<T>(handler)`; engine events `SceneLoaded`, `AdditiveSceneChanged`, `GamepadChanged`, `PlayerChanged`, `AnimationMarker`, `SequenceEvent`, `WindowResized` (see below) |
| Localization | `Loc.Get(key)`, `Loc.Language` (sets the player setting), `Loc.Languages` (`Docs/Localization.md`) |
| Save games | `SaveGame.Save(slot, name)`, `Load`, `Delete`, `Exists`, `List()` (`SaveSlot`: slot, name, scene, date, playtime), `IsWriting`, `Playtime`; `[Save]` keeps a non-public field in save games (`Docs/Save-Games.md`) |
| Settings | The player's settings (`Docs/Player-Settings.md`): `Quality` / `QualityPresets`, `GetVolume` / `SetVolume(AudioBus, v)`, `Language`, `TextScale`, `ReduceMotion`, `Rebind(action, binding, control, part)`, `GetBindingDisplay`, `ResetBindings`, the game's own `GetFloat` / `SetFloat` / `GetInt` / `GetBool` / `GetString` (and setters, under `Game.`), `GetJson` / `SetJson`, `Save`, `Reset` |
| Physics | `Physics.Raycast(origin, dir, maxDistance, out RaycastHit)`, and with a layer mask (`Physics.LayerMask(layers)`), `RaycastAll`, `SphereCast`, `OverlapBox(center, halfExtents, euler)`; 2D: `Physics2D.Raycast`, `OverlapCircle`, `OverlapPoint`, `Rigidbody2D.AddForce/Velocity`; `Rigidbody.AddForce(force, ForceMode)`, `Velocity` (set before the body exists applies when it is created) and `Teleport(position, forward, up)` (keeps velocity) |
| Character | `CharacterController.SetMoveInput`, `Move`, `Jump`, `IsGrounded`, `Velocity` (get/set), `MaxSpeed`, `Teleport(position, forward)` (through physics; a Transform move is undone by interpolation between steps) (`Docs/Physics.md`) |
| Math | `Vector3` (`Zero`/`Up`/`Forward`…, `Normalized`, `Dot`, `Cross`, `Distance`, `Lerp`), `Mathf` (`Clamp`, `Atan2`, `Deg2Rad`, frame-rate independent `Damp`) |
| Any component | `Entity.GetField<T>( component, path, fallback )`, `SetField( component, path, value )` and the raw `GetFieldJson` / `SetFieldJson`: any reflected field by path (`"Light", "Intensity"`, `"Color.0"`), or any key of a hand-serialized component's JSON. Vectors are `[x, y, z]` arrays and enums are names. Setting calls the component's `OnPropertyChanged` |
| Navigation | `NavMeshAgent.SetDestination`, `IsStopped`, `ResetPath`, `Warp`, `HasPath`, `HasArrived`, `RemainingDistance`, `Velocity`, `DesiredVelocity`, `Speed`; `Navigation.FindPath`, `SamplePosition`, `Raycast`, `GetRandomPoint` (`Docs/Navigation.md`) |
| Camera / UI | Clear colour; `Camera.SetClipPlane(point, normal)` / `ClearClipPlane()` (hide everything behind a world plane, for portals and mirrors); `Camera.TargetTexture` (a `.rendertexture` path the camera renders into instead of the screen; empty renders normally) and `UpdateInterval` (every Nth frame), properties over `GetField` / `SetField` (`Rendering-Pipeline.md`, Render textures); `Mesh.ShowCameraView(camera)` (the mesh becomes a screen-space window onto that camera's view, see `Rendering-Pipeline.md`); `UI.Model` view models for RmlUi documents |
| ImGui | A subset for `OnEditorInspect`, plus `Begin`/`End`/`Text`/`Checkbox`/`Button` for a script's own debug window. Those do nothing in builds without ImGui (Retail), so a debug window never crashes a shipped game |

### Console commands (PLAY-10)

Write `[ConsoleCommand("name", "help")]` on a static method that takes `string[]` (or nothing) and returns a string (printed) or nothing.
- **Where they come from:** the developer console (`Source/Engine/DevConsole.h`) finds these in the engine's (ScriptCore's own, such as `game-tests`), the game's and the mods' assemblies. They're reached through `ScriptBridge.GetConsoleCommands` / `RunConsoleCommand`.
- **Opening it:** `~` in game builds (Tab completes names), the edscript `console <line>`, or `--console "<line>"` at start-up.
- **Example:** `Assets/Scripts/Tests/ConsoleProbe.cs`, exercised by `ConsoleFlows`.

### Play-mode tests (CS-12)

`[GameTest]` marks a static method with no parameters, returning `Task` (or `void`), in the game's
or a mod's scripts. `game-tests [filter]` in the console runs them in Play, one after another, by
class then method name: each loads its scene first (`[GameTest("Assets/Scenes/Main.lvl")]`, waiting
for `SceneLoaded` and one frame for its scripts' `OnStart`; no scene runs in place), then runs
with a timeout in game time (`TimeoutSeconds`, 30 by default).

```csharp
[GameTest("Assets/Scenes/Main.lvl")]
public static async Task The_hazard_hurts_and_the_medkit_heals()
{
    var health = World.Find("Player").GetScript<IHealth>()!.Health;
    World.Find("Player").GetComponent<CharacterController>().Teleport(hazard, forward);
    await GameAssert.Eventually(() => health.Current < health.Max, 3f, "the hazard hurt");
}
```

- **Asserts:** `GameAssert.True`, `Equal`, `Near` (floats, vectors) and `await GameAssert.Eventually(condition, seconds, what)`; a failed one throws `GameTestFailure`, and any other exception fails the test with its type and message.
- **Output:** `[GameTest] PASS Class.Name (0.1 s)` or `[GameTest] FAIL Class.Name: reason` per test, then `GameTests: N passed, M failed`. `GameTests.LastResults` has them too.
- **In CI:** a flow plays, runs `console game-tests` and waits for the summary line (`wait-log GameTests: 4 passed, 0 failed | 2400`). The FirstPerson template's `Assets/Scripts/Tests/FirstPersonTests.cs` runs this way in RunChecks' Templates step.
- **Input:** `await SimulatedInput.Hold(KeyCode.W, 1f)` holds a key for game time, `Tap` for one frame, and `Play(recording)` replays a timeline such as `SimulatedInput.Recording.Parse("0 +W; 0.5 +Space; 0.6 -Space; 2 -W")`. They sit on `Input.Simulate(key, held)` / `Input.ReleaseSimulated()`, which work like the `input-hold` edscript command: actions read the key as held, focus or not, while the raw `Input.IsKeyDown` doesn't see it. `using (SimulatedInput.Record()) { ... }` logs the real keyboard's changes as a recording to paste into a test. Each test releases its simulated keys when it ends. A test can also move actors directly (`Teleport`). `FirstPersonTests.Holding_W_walks_the_player_forward` uses `Hold`.

### Data assets (CS-7)

A `DataAsset` subclass (`Modules/ScriptCore/Source/Core/DataAsset.cs`) is tuning data in a file: `{ "Type": "WeaponStats", "Fields": { ... } }`, read and written through the VFS (`Asset_ReadText` / `Asset_WriteText`).
- **In the editor:**
  - **Create > Data Asset > <Type>** in the asset browser lists the game's classes (`ScriptBridge.GetDataAssetTypes`).
  - Selecting an `.asset` file draws its fields in the details pane with the script inspector's rows (`InspectDataAsset`). It re-reads the file first, and saves as you edit.
- **References and caching.** Scripts reference an asset with an `AssetRef` (`[AssetFilter("asset")]`) and call `Load<T>()`. Loads are cached per path, so every holder shares one instance.
- **Reloading.**
  - When the file changes while playing (`AssetsChanged`, tools builds), the instance re-reads in place, `OnLoaded` runs again, and holders see the new values.
  - A hot reload of the scripts drops the cache.
- **Proof:** `DataAssetProbe.cs` + `DataAssetFlows.edscript`, which creates, edits, references and live-edits `Assets/Scenes/Tests/ProbeStats.asset` (the `data-asset-create`, `data-asset-set` and `data-asset-assert` commands) and captures the details pane.

### Async, coroutines, timers and tweens (CS-6)

`Modules/ScriptCore/Source/Core/Async.cs` runs all four on the game clock, on the main thread.
- **The tick.** `ScriptBridge.SchedulerTick` runs once a frame, before the scripts' `OnUpdate`, with game delta time, so pausing stops them and `--frame-time` makes them frame-exact. It runs the posted continuations, then every coroutine, timer and tween, then the continuations those released.
- **Await.** A main-thread `SynchronizationContext` is installed when Play starts, so any `await` resumes on the main thread inside that pump. That includes `Task.Delay`, which runs on the wall clock (before CS-6 it resumed on a pool thread).
- **Frame counting.**
  - The first tick after `OnStart` is frame 1.
  - `Async.Seconds(0.5f)` at 0.02 s frames completes on frame 25.
  - A coroutine's `WaitForSeconds` counts from the frame after the one it yielded on.
  - A tween's last call is exactly its target.
- **Lifetime.**
  - Whatever a script owns (its `StartCoroutine` / `After` / `Every`, and anything started with `owner: this`) is cancelled when the script is destroyed.
  - Everything stops on Stop and on a hot reload. An `AsyncHandle.Task` completes either way.
- **Ease.** `Linear`, `SmoothStep` and `Hold` match `SequenceInterpolation`; there are also quad, cubic and `OutBack` curves.
- **Proof:** `AsyncProbe.cs` + `AsyncFlows.edscript`, frame-exact at `--frame-time 0.02`. RunEditorFlows reads `--frame-time` from a flow's header.

### Events (CS-8)

`EventBus` (`Modules/ScriptCore/Source/Core/EventBus.cs`) carries typed events between scripts: any type is an event, usually a `readonly record struct`.
- **Delivery:** `Publish` delivers at once, in subscription order. A throwing handler is reported (stack and all), and the rest still run.
- **Lifetime:**
  - A script's handlers go when the script is destroyed: `ScriptOnDestroy` drops every delegate whose target is that script. That covers methods and lambdas that only use `this`; a lambda that captures locals needs its token disposed.
  - Every handler goes on a hot reload, so scripts subscribe again in `OnStart` / `OnReload`.
- **Engine events.** `ScriptCore` listens for engine events and queues them (at most 256, cleared on Stop). It hands them to `ScriptBridge.DispatchEngineEvents` as one JSON array before the next update's `OnUpdate`, and they arrive typed:

| Event | When |
|---|---|
| `SceneLoaded(Path)` | The main scene loaded |
| `AdditiveSceneChanged(SceneId, Path, Loaded)` | An additive scene loaded or unloaded |
| `GamepadChanged(Slot, Connected, Name)` | A pad connected or left |
| `PlayerChanged(Player, GamepadSlot, Change)` | A local player joined, lost or regained its pad |
| `AnimationMarker(Entity, State, Name)` | An Animator passed a marker |
| `SequenceEvent(Player, Receiver, Kind, Name, Payload, Number, Frame, Clip)` | A Sequencer marker was crossed, or playback Started / Finished / was Interrupted / Looped (GAME-4; a receiver's `ISequenceReceiver` script hears it first, see [Sequences](Sequences.md#event-markers-game-4)) |
| `WindowResized(Size)` | The window resized |
| `AssetsChanged(Paths)` | Project files changed while playing (tools builds; data assets re-read first) |

- **Proof:** `EventProbe.cs` + `EventFlows.edscript`.

### Platform status

| Platform | Status |
|----------|--------|
| Win64 | `DEFINE_ME_DOTNET` when `Globals.DOTNET_Win64_Dir` (the `Microsoft.NETCore.App.Host.win-x64` native pack) exists. `nethost` is copied to the output, and the Sharpmake C# projects are in the solution. The CLI build and hot reload paths are written but untested there |
| Linux | ✅ Verified. Needs `Globals.DOTNET_Linux_Dir`. The SDK environment (`nix/flake.nix`) exports `DOTNET_ROOT` and finds the host pack's `runtimes/linux-x64/native` directory. Executables link `libnethost.so` with an rpath to it (`Tools/BaseProject.sharpmake.cs`), and the scripts build with the CLI |
| macOS | ❌ No `DEFINE_ME_DOTNET` path yet, so scripting compiles out. The CLI build would carry over; the host pack lookup and link are what's missing |
| UWP | ❌ Not wired |

### The script world (NET-0)

Script calls act on `ScriptBindings::GetWorld()`:

- **Default:** the engine's game world (`ScriptEngine::SetWorld`).
- **Scoped:** inside a `ScriptBindings::WorldScope` on the current thread, they act on the scope's world. Scopes nest and are per-thread, so a server or rollback simulation can run its scripts against its own world.
- **Core lookup:** bindings reach physics, 2D physics and navigation through `ScriptBindings::Physics()` / `Physics2D()` / `Navigation()`, which are that world's cores, not `GetEngine()`'s. `ScriptCore` syncs its own world's cores before `OnStart`.

### API reference (CS-10)

`Docs/API/ScriptCore.md` is generated from ScriptCore's XML doc comments: a `/// <summary>` on a public type or member is what puts it in the reference (and in IDE tooltips). `Engine/Tools/CheckScriptDocs.py` builds ScriptCore with documentation in a scratch folder and does three things:
- **Bad comments:** it fails on badly formed doc XML (escape `<`, `>` and `&`).
- **The baseline:** it fails when any file has more undocumented public members than `Tools/ScriptDocsBaseline.json` allows. It started at 1011 and reached 0 on 2026-10-30, so every new public member needs a `///` comment (or `<inheritdoc/>` on an override). `--update` only ever lowers it.
- **The page:** it fails when the page no longer matches the comments; `--markdown` rewrites it.

RunChecks runs it as its "Script API docs" step. ScriptCore.csproj generates `ScriptCore.xml` beside the DLL, so IDEs show the same summaries as tooltips. The generated interop (`Generated/*.g.cs`) and the SDL key enums opt out with `#pragma warning disable CS1591`.

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
        transform.Rotate(new Vector3(0f, Speed * deltaTime, 0f));
        if (Input.WasActionPressed("Jump"))
        {
            Audio.PlayOneShot("Assets/Sounds/Jump.wav");
        }
    }
}
```

Save it anywhere under `Assets/`. The editor builds it on the next start, or hot reloads it if already running. Then add a `ScriptComponent` naming `Spinner` to an entity.

**Create > C# Script (ME-237)** in the asset browser writes one from a template (`Modules/Havana/Source/Utils/ScriptTemplates.h`):
- **The templates:** Script, Networked Script (partial, with a `[Replicated]` field and a `[ServerRpc]`), UI Controller (drives a `data-model`), Data Asset and a plain Class.
- **Naming:** the class name is unique across the project, because scripts are found by class name.
- **Namespace:** the class goes in a file-scoped namespace named after the project (`namespace MitchGame;`), so scripts in the global namespace need a `using` to refer to it. `Assets/Scripts/Tests/ScriptProbe.cs` and `Assets/Scenes/Tests/ScriptTest.lvl` are a working example that `ScriptFlows.edscript` drives headlessly.

`Transform.Rotate` composes a rotation in degrees about the entity's local axes directly with its quaternion. Use it for continuous rotation. `Transform.Rotation` sets an absolute local rotation in Euler degrees; its getter can return an equivalent Euler representation (for example, `(0, 100, 0)` becomes `(180, 80, 180)`). Reading that representation and replacing just one axis can make rotation alternate between different poses.

### Generate field schemas and serializers (`[GenerateSchema]`)

Mark a **partial**, non-nested, non-generic script class with `[ScriptCore.Generation.GenerateSchema]` and the CS-9 Roslyn source generator (`Modules/ScriptCore/Generators/`, wired into every script build by `MitchEngine.Generators.targets`: `Project/Game.Script.csproj` imports it, the Sharpmake `GameScript` project pulls it in via `CustomTargetsFiles`) emits into the class at compile time:

- `static TypeSchema Schema` — field names, kinds and boxed accessor lambdas (no reflection), registered into `SchemaRegistry` by a module initializer. The registry holds types weakly, so hot-reloaded assemblies still unload.
- `SaveFields(Utf8JsonWriter)` / `LoadFields(JsonElement)` — the same JSON object shape `ScriptSerializer` produces via reflection (tested byte-for-byte), with tolerant loads that skip unknown or mistyped entries.
- `Schema.Hash` — a stable FNV-1a over names and kinds; two builds agree exactly when their fields agree (net handshakes, save versioning later).

Supported field kinds: `bool`, `int`, `float`, `double`, `string`, int-backed enums, `Vector2`, `Vector3`. Public instance fields only; `[HideInInspector]` fields stay unpersisted like the reflection path. Misuse gets diagnostics: **MEGEN001** type not partial (error), **MEGEN002** unsupported or readonly field skipped (warning), **MEGEN003** nested or generic type (error). Everything generated is reflection-free, keeping shipped C# AOT-friendly.

Tests: `Modules/ScriptCore/Generators.Tests/` (xunit, run by RunChecks and CI with build artifacts under `.tmp/`) snapshots generated source, checks the diagnostics, compiles the output in-memory, and runs a live `[GenerateSchema]` probe against `ScriptSerializer` for shape parity. The game's `Assets/Scripts/Tests/ScriptProbe.cs` uses it in a real script build, asserted by `ScriptFlows.edscript`.

### Networked scripts (`[Replicated]`, `[Predicted]`, RPCs; NET-6)

A second generator, `NetGenerator`, picks up `partial` `Script` classes with `[Replicated]` / `[Predicted]` fields or `[ServerRpc]` / `[ClientRpc]` methods. It gives them `INetScript`:

- fixed-size, allocation-free state serializers;
- RPC send stubs and dispatch to `On{Name}` handlers;
- a `__NetSchemaHash` the session handshake compares.

It reports MEGEN004–010 for what it can't generate. The C# side is `Modules/ScriptCore/Source/Net/` (`Net`, the attributes, `NetWriter` / `NetReader`, `NetBridge.cs`). The full model is in [Networking.md](Networking.md) ("C# networking (NET-6)"). The game's `Assets/Scripts/Tests/NetProbe.cs` is the end-to-end probe (Tools/RunNetSmoke.py).

## Starter kit (GAME-7)

`ScriptCore/Source/Kit/` (namespace `ScriptCore.Kit`) holds ready-made gameplay pieces. The FirstPerson template uses them.

**Engine scripts are attachable.** `ScriptRegistry` lists ScriptCore's own concrete scripts after the game's and the mods'. A scene can name `Door` or `Pickup` directly, and a game class with the same name wins the short name. The full name (`ScriptCore.Kit.Door`) always finds the engine's.

An entity carries one script. So what an *actor* has is a plain object that its script owns and exposes through an interface:

| Piece | What it does |
|-------|--------------|
| `Health` (via `IHealth`) | `Damage`, `Heal`, `Kill`, `Reset`, `Invulnerable`; C# events plus `DamagedEvent` / `DiedEvent` / `HealedEvent` on the EventBus |
| `Inventory` (via `IInventory`) | named item counts: `Add`, `Remove`, `Has`, `Count` |
| `Interactor` | `Update(self)` each frame: picks the nearest `Interactable` in range and in front, shows its prompt through a UI model (`prompt`, `visible`), and uses it on the `Interact` action |

The *world* pieces are scripts. They find those objects on whatever touches them with `other.GetScript<IHealth>()`:

| Script | What it does |
|--------|--------------|
| `Pickup` | adds an item to the collector's Inventory and/or heals it, then disappears |
| `Door : Interactable` | slides by `OpenOffset` when used, optionally locked until the user holds `RequiredItem`. Give it a kinematic Rigidbody. |
| `DamageZone` | damages on entering and per second inside a trigger |
| `Checkpoint` | entering it sets the actor's respawn point. `Checkpoint.Respawn(actor, fallback)` moves the actor there and refills its health. |
| `Spawner` | instantiates a prefab every `Interval`, up to `MaxAlive` (and `Total`) |
| `Damageable` | Health for props with no script of their own; destroyed on death |

| `Conversation : Interactable` | runs a `.dialogue` file through the stock dialogue box (GAME-8, below) |
| `VirtualCamera` | GAME-1 camera rig. Modes: Fixed, Follow, Orbit (Look turns it; pulls in at walls) and Platformer2D (dead zone). It has a Priority, an optional confiner box and field of view, and the BlendSeconds used to take over. |
| `GuardBrain` | GAME-3 sample AI on a NavMeshAgent. It patrols `Waypoints`, chases `Target` on sight, catches it, then searches the last known spot. `State` shows what it's doing, and every change is logged. |
| `CameraBrain` | goes on the entity with the Camera. It shows the highest-priority live rig and blends (smoothstep) when that changes. `CameraImpulse.Shake(strength, seconds)` adds shake. |

`Entity.GetScript<T>()` (any script type or interface) scans the live scripts, so call it on events rather than for every entity every frame.

The kit's tests are `Generators.Tests/KitTests.cs` and `ScriptRegistryTests.cs`. `Tests/Templates/FirstPerson/FirstPersonFlows.edscript` plays the kit in the template:
1. Walk over the key.
2. Open the locked gate with E.
3. Die in the hazard.
4. Respawn at the checkpoint.

## AI (GAME-3)

`ScriptCore/Source/AI/`:
- **Behavior trees:** built in C# with `BT.*` (`Selector`, `Sequence`, `MemorySequence`, `Action`, `Condition`, `Not`, `Wait`, `Cooldown`, `Do`) and ticked with a `BTContext(Blackboard, deltaTime)`.
  - `Selector` and `Sequence` re-check from the top each tick, so a higher branch interrupts a running lower one and gets `Reset()`.
  - `MemorySequence` resumes where it was.
- **`Perception`:** sight (range, field-of-view cone, line of sight by raycast from `EyeHeight`) and hearing (`Noise.Make(position, radius)`), with a last-known position that expires after `MemorySeconds`.
- **`GuardBrain`** (kit) puts these together.
- **Tests:** `Generators.Tests/AITests.cs`, plus `Assets/Scenes/Tests/GuardFlows.edscript` (game) on NavTest. The guard there patrols, sees the Intruder the flow moves into view, catches it, loses it behind a wall, searches, and returns to its patrol.

## Dialogue (GAME-8)

`ScriptCore/Source/Dialogue/` handles conversations written as text (`.dialogue` files, in the spirit of Yarn):

```
title: Gate
---
Guard: Halt! Who goes there?
-> A friend.
    <<set $trusted = true>>
    Guard: Then pass, friend.
-> None of your business. <<if not $trusted>>
    <<jump Refused>>
<<if $trusted>>
    Guard: Mind the stairs, {$name}.
<<endif>>
<<event open-gate north>>
===
```

**The format:**
- A node is `title:`, then `---`, its lines, and `===`.
- A line is `Speaker: text`.
- A run of `-> option` lines is a choice, each followed by its indented body. A trailing `<<if expr>>` hides an option unless the expression is true.
- **Commands:** `<<jump>>`, `<<set>>`, `<<if>>` / `<<elseif>>` / `<<else>>` / `<<endif>>`, `<<event name args>>` and `<<stop>>`.
- **Comments** start with `//`.
- **Expressions** use `not`, `and`, `or`, comparisons, `+ - * /` and parentheses. An unset variable is `false`, which counts as 0.
- **Localization:** lines and options are localization keys. `DialogueRunner` translates them through `Loc` before filling in `{$var}`.

**The pieces:**

| Class | What it does |
|-------|--------------|
| `DialogueScript.Parse` | parses a file; `FormatException` names the file and line |
| `DialogueRunner` | `Start(node)`, `Continue()`, `Choose(i)`; `Current` is Line, Choice or End. `Variables` can be shared. `Event` and the EventBus carry `DialogueEvent` and `DialogueEndedEvent`. |
| `DialogueView` | drives the `dialogue` UI model (`visible`, `speaker`, `text`, `choosing`, `choice0..3`, `has0..3`; events `dialogue_continue`, `dialogue_choose_0..3`), which the stock `Engine/Assets/UI/Dialogue/Dialogue.rml` binds |
| `DialogueFiles.Load` | reads and caches a file through the VFS |
| `Conversation` (kit) | ties them together |

**Tests:**
- `Generators.Tests/DialogueTests.cs` covers the language and the runner.
- `Assets/Scenes/Tests/DialogueFlows.edscript` (game) clicks through `Assets/Dialogue/Tests/Gate.dialogue` in the real UI.

## Caveats & Fragility

- **The ABI guard** (ME-298) has several layers:
  - **Hash:** the generator hashes every slot's signature in order. `kScriptEngineAPIHash` in C++ and `EngineAPIBindings.AbiHash` in C# are compared when scripting starts (`ScriptBridge.GetEngineAPIHash`), so a stale ScriptCore.dll is refused with both hashes named.
  - **Layout:** the sizes of the structs that cross (Entity, Vector2/3/4, RaycastHit and its field offsets) are `static_assert`ed in C++ and checked in C# (`ScriptAbiLayout`) when the API is bound.
  - **The other direction (ME-299):** the managed entry points the engine calls are generated too, from `Source/Scripting/ScriptBridge.def`. C++ gets a `ScriptBridgeAPI` struct, and C# gets the same struct plus `ScriptBridge.GetBridge`, which assigns each `[UnmanagedCallersOnly]` method into its typed `delegate* unmanaged` field. So a C# entry point whose signature drifts from the manifest fails to compile (CS8757). C++ fetches every pointer in one call, refusing a ScriptCore built from another manifest (`kScriptBridgeAPIHash`). To add an entry point: append to ScriptBridge.def, write the method, regenerate.
  - **Append-only:** add slots at the end. Never hand-edit the generated files; `--check` catches drift between the manifest and the outputs.
- **Bindings are `noexcept` (ME-301):** the generated slots are `noexcept` function pointers, so every `Eng_*` binding has to be declared `noexcept` or it won't compile. A C++ exception can then never unwind into the managed frames that called it, which is undefined behaviour in CoreCLR; it terminates at the throw with a native stack instead. The JSON-facing bindings (`Component_GetField` / `SetField`, `Settings_Get` / `Set`, `SaveGame_List`) catch `std::exception` themselves (malformed field paths, invalid UTF-8 in `dump()`), log it, and return failure.
- **Structs never come back by value:** manifest return types are `void` / `bool` / `int` / `float` / `double` only, and the generator refuses anything else. MSVC x64 returns a type with a constructor (EntityID) through a hidden pointer, so results go through out pointers. Structs passed by value as arguments must be trivially copyable.
- **Script lookup (MOD-2):** by full name first, then by class name while that's unique. When two classes share a name (a mod's and the game's), the game's wins the short name and a warning says so. Scenes save the full name of a clashing class (`ScriptRegistry.NameOf`).
- **Renames need `[FormerName]`.** A scene names its scripts by class name and their fields by field name. Renaming a script file in the asset browser renames its class and adds the `[FormerName]` itself when a scene or prefab still names the old class; renames made elsewhere need it by hand. Put `[FormerName("OldClass")]` on a renamed script class: scenes that name the old class create the new one, and `ScriptComponent` adopts the new name (logged) so the next save writes it. `[FormerName("oldField")]` on a field reads a value saved under the old key. `ScriptTest.lvl` / `RenameProbe.cs` exercise both in `ScriptFlows`.
- **Handles are bare ints.** They stay stable across reloads, and `IsHandleAlive` validates them, but there is no generation counter.
- **Game builds don't compile scripts.** The DLLs must already sit in the assembly directory, or scripting logs a warning and stays off.
- **The .NET SDK must be on `PATH`** for tools builds to compile. Without it the existing DLLs are used as-is.
- **All script calls are synchronous on the main thread.** A slow `OnUpdate` is a frame hitch; there is no sandboxing or time budget.
- **Field access by name goes through JSON.** It reaches every component, but a typed wrapper (like `Rigidbody`) is faster for per-frame use. Wrong names return the fallback or `false`, not an error.

## Related Docs

- [Networking.md](Networking.md): `[Replicated]` / `[Predicted]` fields, RPCs and `Net` (NET-6).
- [Scripting-Interop-Notes.md](Scripting-Interop-Notes.md): s&box's InteropGen compared with our ABI generator, and what to adopt.
- `Docs/Build-System.md`: `DOTNET_*_Dir` detection, nethost linking, flake.nix
- `Docs/Architecture.md`: where scripts sit in the frame
- `Docs/Editor-Havana.md`: the inspector, `--editor-exec` (`wait-log`, `replace-in-file`)
- `Docs/Input.md`: the action names scripts read
- `Docs/Physics.md`: `SyncNow`, raycasts, rigidbody forces
