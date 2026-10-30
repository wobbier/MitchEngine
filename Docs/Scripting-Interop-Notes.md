# Script interop notes: s&box's InteropGen vs ours

Research notes (2026-10-30) comparing Garry Newman's [InteropGen post](https://garry.net/posts/interopgen) with our C# ↔ C++ layer: `Source/Scripting/ScriptAPI.def` → `Tools/GenerateScriptAPI.py` → `ScriptEngineAPI.generated.h` / `EngineAPIBindings.g.cs`, plus the hand-written `ScriptBridge` direction (`ScriptEngine.cpp` ↔ `Modules/ScriptCore/Source/Engine.cs`).

## What the post describes

Garry's post describes InteropGen, the s&box tool that turns hand-written C++-like `.def` files into both halves of the interop. The `.def` files declare native accessors (globals), native classes and their methods, managed static or instance classes that C++ can call, and structs by name only. From those it generates C++ thunks that forward to the real engine call (inline bodies in the `.def` can adapt types without touching engine headers) and C# static wrappers over `delegate* unmanaged` pointers that null-check and marshal strings through `Interop.GetPointer`. At startup the two sides swap arrays of function pointers in both directions. A hash of the definitions is checked on both sides, and init fails on a mismatch. Struct sizes are also checked at init, because struct layouts are never declared and must simply match. Managed objects reach C++ as `uint` handles, and native objects reach C# as pointers inside wrapper classes. He deliberately does not parse C++ headers (the headers would have to bend to fit the interop), calls Roslyn source generators for interop "another layer of bullshit", and binds as little as possible. The post doesn't cover SuppressGCTransition or how exceptions cross the boundary.

Our setup is the same core idea, at about half the scope:
- `Engine/Source/Scripting/ScriptAPI.def` is a one-line-per-function manifest.
- `Engine/Tools/GenerateScriptAPI.py` emits the C++ `struct ScriptEngineAPI` and the C# `struct EngineAPIBindings` (`delegate* unmanaged<>`). `--check` catches stale outputs.
- C++ fills the slots in hand-written `Register_*Bindings` (`Eng_*` functions). `ScriptEngine.cpp` refuses to start if any slot is null. It then hands the table over with `SetEngineAPI(api, size)`, and C# checks only `sizeof`.
- The C++→C# direction is not generated at all. `ScriptEngine.cpp` has about 30 hand-written `Fn*` typedefs, loaded one by one through `ScriptHost::LoadFunction("ScriptCore.ScriptBridge, ScriptCore", name)`. Nothing checks those typedefs against the `[UnmanagedCallersOnly]` signatures in `Engine.cs`, so for example `FnCreateScript = int(*)(const uint8_t*, EntityID)` and C# `CreateScript(byte*, Entity)` only match because someone kept them matching by hand.
- The C# wrappers in `ScriptCore/Source/Components` and `Core` are hand-written. 87 call sites allocate a `byte[]` for every string argument through `Engine.Utf8`.
- Our Roslyn generators (`SchemaGenerator`, `NetGenerator`) have nothing to do with interop. They generate per-script field schemas and net serializers from user code, which needs the compiler's semantic model. That fits Garry's view: a plain script is enough for the interop itself.

## How it relates to ours

What is the same:
- A hand-written definition file is the single source of truth, rather than parsing C++ headers.
- Raw `delegate* unmanaged` function pointers are used instead of DllImport or `Marshal`.
- Strings cross as UTF-8 `byte*`.
- `bool` crosses as a byte, which avoids the non-blittable `bool` trap.
- Structs are blittable and must match on both sides.
- A function-pointer table is exchanged once at startup.
- Hot reload swaps only the game assembly. ScriptCore and the API table stay loaded, the same split as s&box's engine and game assemblies.
- Native objects are referenced by safe ids: our generational `EntityID` plays the role of their handles.

Where we're stronger:
- Unfilled slots are detected at init. s&box instead throws "Function Pointer Is Null" on every call.
- Every `[UnmanagedCallersOnly]` catches exceptions and routes them to `ReportException`.
- `--check` keeps the generated files from drifting.

Where we're weaker, roughly in order:
1. The C++→C# direction is hand-written and checked by no compiler. s&box generates both directions.
2. The ABI guard is `sizeof` only, with no content hash, so a changed parameter type or a reorder inside the same slot count gets through.
3. Struct layouts are never checked. `Vector3` is a union over `glm::vec3` with no `static_assert`, and `RaycastHit` is mirrored by hand.
4. The generator would accept a struct return type. MSVC x64 returns a type with a user-declared constructor (`EntityID` has `EntityID(uint32, uint32)`) through a hidden pointer, while .NET expects it in RAX. That is the general shape of the EntityID-by-value break we just hit.
5. No generated C# marshalling layer exists, so string handling is copy-pasted and allocates.
6. Nothing stops a C++ exception from unwinding through managed frames.
7. Script handles are bare ints with no generation counter.

## Ideas, by value

| Idea | Effort | Verdict |
|---|---|---|
| Content hash in the ABI handshake (the same idea as s&box's definition hash) | S | Yes |
| Reject struct returns by value in the generator, and generate layout checks for every type that crosses | S | Yes |
| Generate the C++→C# direction from the manifest too, as one struct that C# fills (s&box's two-way pointer exchange) | M | Yes |
| A generated typed C# layer that handles strings and out-strings without allocating | M | Yes, but not urgent |
| Make generated slots `noexcept` so C++ exceptions can't unwind into managed frames | M | Probably yes, as a mechanical one-off |
| Actually verify that the old script ALC unloads after a hot reload | S | Yes |
| Clean up leftovers in the existing interop | S | Yes, as a Linear ticket that is quick to close |
| Add a generation counter to script handles | S | Maybe |
| An opt-in `[SuppressGCTransition]` flag for leaf getters | S | Later, and only with numbers |
| Batch the per-frame script callbacks into one transition | M | Later |
| Generate bindings from C++ headers, or move the generator into a Roslyn source generator | L | No |


### Content hash in the ABI handshake (the same idea as s&box's definition hash) (S)

**How it applies:** Have GenerateScriptAPI.py compute FNV-1a over the normalized manifest (ret, name, param types, in order) and emit `constexpr uint64_t kScriptAPIHash` into ScriptEngineAPI.generated.h and `public const ulong Hash` into EngineAPIBindings.g.cs. Today `SetEngineAPI(api, size)` in Engine.cs only compares sizeof. Either add an optional bridge export `GetAPIHash()`, loaded like the optional Net* ones so older DLLs keep working, or extend SetEngineAPI with a hash parameter, then fail with a message naming both hashes. This catches a stale ScriptCore.dll next to a newer exe: a shipped `dotnet/` folder, `.tmp/Scripts/<Exe>-<Config>` for shared executables, or the Player.

**Worth it:** Yes. About 20 lines, and it turns silent memory corruption (an int read as a float, swapped Vector3* arguments) into a clear startup error. Fold the hash of idea 3's bridge manifest into it once that exists.

### Reject struct returns by value in the generator, and generate layout checks for every type that crosses (S)

**How it applies:** In GenerateScriptAPI.py, allow return types only from {void, bool, int, float, double}. Today TYPES lets `Entity` be a return type, which on MSVC x64 is returned through a hidden pointer because EntityID has a user-declared constructor, while C# expects RAX. Keep Entity, Vector and RaycastHit by value as arguments at most, and outputs always by pointer, as the manifest already does by convention. Then emit layout checks. On the C++ side: `static_assert(sizeof(EntityID)==8 && alignof(EntityID)==4 && std::is_trivially_copyable_v<EntityID> && std::is_standard_layout_v<...>)`, plus `offsetof` checks for ScriptRaycastHit, Vector2, Vector3 and Vector4. Vector3 is a union over glm::vec3 with no size assert, so a GLM_FORCE_ALIGNED or SIMD define would silently make it 16 bytes. On the C# side: a generated table of `sizeof(T)` values, compared in SetEngineAPI against a size array that C++ passes. Apply the same rule to the hand-written bridge (idea 3), where EntityID is passed by value into CreateScript and ScriptOnCollision. That is legal as an argument but sits one refactor away from being a return value. The fully defensive variant has Entity cross as uint64_t/ulong and converts in generated thunks, but that touches about 150 Eng_ signatures, so start with the assert-and-reject version.

**Worth it:** Yes. This is the direct fix for the class of bug we just hit, and it's cheap. Also add `const Vector2*` and `const Vector4*` to TYPES: Physics2D_*, Rigidbody2D_* and Sprite_SetColor currently take non-const pointers for inputs.

### Generate the C++→C# direction from the manifest too, as one struct that C# fills (s&box's two-way pointer exchange) (M)

**How it applies:** Add a `## managed` section, or a ScriptBridge.def, listing the bridge entry points: CreateScript, ScriptOnUpdate, ScriptOnCollision, the Net* set and so on. From it, generate a C++ `struct ScriptBridgeAPI` that replaces the hand-written `Fn*` typedefs and `struct ScriptAPI` in ScriptEngine.cpp, and a C# `partial class ScriptBridge` with a generated `[UnmanagedCallersOnly] static int GetBridge(ScriptBridgeAPI* out, int size, ulong hash)`. That function assigns `out->CreateScript = &CreateScript;` into typed `delegate* unmanaged<byte*, Entity, int>` fields, so the C# compiler rejects any [UnmanagedCallersOnly] method whose signature drifts from the manifest. LoadAPI becomes one LoadFunction call instead of about 30. Each of those currently goes through hostfxr's reflection lookup, and its result is unchecked against the C# side. Optional slots (NET-6) become 'present if the size covers them'. The dead commented-out typedefs and LoadFunction calls (GetFieldCount, RestoreScript and others) go away.

**Worth it:** Yes. Of all the gaps against s&box, this one most needs fixing: the hand-written direction is the only part of the ABI that no compiler checks, and it's where EntityID crosses by value. Do it after ideas 1 and 2 so it gets the hash and the layout checks for free.

### A generated typed C# layer that handles strings and out-strings without allocating (M)

**How it applies:** 87 call sites do `fixed (byte* s = Engine.Utf8(name))`, allocating a byte[] per call (for example Animator.SetFloat every frame means garbage every frame). Strings returned into a buffer (Entity_GetName, Animator_GetState, Loc_Get and others) repeat the same two-call 'query size, allocate, fill' pattern by hand. Have the generator also emit `internal static unsafe partial class Native` with one method per manifest function that takes `string` / `ReadOnlySpan<char>`. It would encode into a `stackalloc` buffer for short strings and fall back to ArrayPool, the way Engine.Log already does, and wrap `int X(..., byte* out, int size)` as a helper returning `string`. The hand-written public API (Transform.Position etc.) stays hand-written but calls `Native.X(...)`. Optionally add `ReadOnlySpan<byte>` overloads so hot callers can pass `"Speed"u8` literals.

**Worth it:** Yes, but not urgent. It removes per-frame GC garbage from the script API and deletes a lot of boilerplate. This is the equivalent of s&box's generated wrappers and `Interop.GetPointer`, without adopting their whole model.

### Make generated slots `noexcept` so C++ exceptions can't unwind into managed frames (M)

**How it applies:** s&box doesn't cover this, but it's the mirror image of what Engine.cs does with ReportException. In C++17 and later, noexcept is part of the function pointer type, so emitting `bool ( *X )( ... ) noexcept;` in ScriptEngineAPI.generated.h makes the compiler reject any Eng_* function not marked noexcept. A throw then calls std::terminate with a clear native stack, instead of unwinding through CoreCLR frames, which is undefined and usually a confusing crash. The json::parse calls already use the non-throwing overload, but `.get<T>()` type errors and component OnDeserialize inside Component_SetField can still throw. Marking about 250 Eng_ functions can be done mostly with sed, and a few try/catch blocks go where throwing is plausible (Component_SetField, Settings_Set, Asset_*).

**Worth it:** Probably yes, as a mechanical one-off. Smaller variant: catch exceptions only in the JSON and field bindings and skip the noexcept enforcement.

### Actually verify that the old script ALC unloads after a hot reload (S)

**How it applies:** Engine.cs keeps `_alcRef = new WeakReference(newAlc)` but never checks it, and its comment ('reload path is still commented out') is stale. In tools builds, keep a weak reference to the previous ALC across ReloadGameAssembly, then a few frames later run GC.Collect() and GC.WaitForPendingFinalizers(). If it's still alive, log a warning: 'previous Game.Script assembly did not unload: a static, EventBus subscription, Task or timer still references it'. Unload leaks build up over long editor sessions and are the main hot-reload failure in s&box-style setups.

**Worth it:** Yes. A cheap guard against slow editor memory growth across reloads. Fix the stale comment at the same time.

### Clean up leftovers in the existing interop (S)

**How it applies:** In ScriptEngine.cpp, remove the commented-out typedefs and LoadFunction lines (FnGetFieldCount, GetFieldInfo, GetInstance*, GetMethod*, RestoreScript) and the unused typedefs. Update Docs/Scripting-DotNet.md's 'Caveats' section: the ABI guard becomes hash plus layout checks once ideas 1 and 2 land. Document the rule 'structs never cross as return values; outputs by pointer'.

**Worth it:** Yes, as a Linear ticket that is quick to close. Do it together with idea 1 or 2.

### Add a generation counter to script handles (S)

**How it applies:** Script handles are bare ints (`_nextHandle++`, validated only by `IsHandleAlive`), a caveat the docs already list. Pack `index | generation << 24`, the way EntityID does, so a stale handle held by C++ (a ScriptComponent after destroy and recreate, or a pending collision callback) can't reach a different instance. The ABI type stays `int`.

**Worth it:** Maybe. Low risk today because handles never get reused (the counter only goes up). Worth doing only if handles start being recycled or pooled.

### An opt-in `[SuppressGCTransition]` flag for leaf getters (S)

**How it applies:** Allow a manifest annotation (for example a trailing `@leaf`) that makes the generator emit `delegate* unmanaged[SuppressGCTransition]<...>` for tiny non-blocking calls that never call back into managed code: Time_GetDeltaTime, Transform_GetTranslation, Entity_IsAlive, Input_IsActionPressed. It skips the managed-to-native GC mode switch, saving roughly 10–20 ns per call. ImGui_*, anything that can raise script events or load assets, and anything that takes locks must never be marked. A wrongly marked call can deadlock the GC.

**Worth it:** Later, and only with numbers. Measure a scene with a few hundred scripts first. The win is real for chatty per-frame getters, but a wrong annotation is a nasty bug.

### Batch the per-frame script callbacks into one transition (M)

**How it applies:** ScriptCore.cpp calls ScriptEngine::ScriptOnUpdate, OnFixedUpdate and OnLateUpdate once per script handle, so each script costs one native-to-managed transition plus a Dictionary lookup in Engine.cs, per callback, per frame. Add bridge entries like `ScriptUpdateMany(const int* handles, int count, float dt)` and keep the per-script exception isolation inside the C# loop.

**Worth it:** Later. It only matters with hundreds to thousands of script instances, so profile with Optick first. If idea 3 lands, the new entries are just more manifest lines.

### Generate bindings from C++ headers, or move the generator into a Roslyn source generator (L)

**How it applies:** Garry rejects both, and our setup agrees with him. Parsing headers (libclang, or ME_FIELD-style annotations) would have to understand Vector3's union, the EntityID constructors, and so on, and would push interop rules into engine headers. A Roslyn generator could only produce the C# half and would complicate the C++ build. Keep the Python manifest generator. Keep Roslyn for what needs semantic information about user scripts (SchemaGenerator, NetGenerator).

**Worth it:** No. Our manifest with a generated table is the right level. Spend that effort on ideas 1–4 instead.

## Tracked

Linear: ME-298 (ABI guard: hash, layout asserts, no struct returns, cleanup; done), ME-299 (generated C++→C# bridge; done: `ScriptBridge.def`), ME-300 (allocation-free strings), ME-301 (noexcept bindings), ME-302 (ALC unload check).
