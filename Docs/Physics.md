# Physics

Physics comes in two engine-owned cores, each built on a library compiled from source as a C17 static library:

- **3D:** `PhysicsCore` runs on **Box3D**. Its bodies and shapes come from `Rigidbody`, the four 3D collider types and `CharacterController`; its joints from `PhysicsJoint`.
- **2D:** `Physics2DCore` runs on **Box2D v3.1** in the world XY plane. Its bodies and shapes come from `Rigidbody2D`, the five 2D collider types and `CharacterController2D`; its joints from `PhysicsJoint2D`.

Both cores step at the fixed rate while the world is started, then interpolate the poses they write to Transforms. Outside play mode, bodies follow their Transforms instead, so queries and gizmos stay in sync. Contacts and triggers are reported through `CollisionEvent`. Layers and a Project Settings collision matrix filter both collisions and queries. Solver work runs on the engine job system. Most of this doc describes the 3D core; [2D physics](#2d-physics-box2d) covers where the 2D core differs.

> Verified against engine commit 8fdd99b1, 2026-10-09 (overhaul Wave 4).

## Overview

| Concept | Component / API | Box3D object |
|---------|-----------------|--------------|
| Simulated / moved / fixed body | `Rigidbody` (`Type` = Dynamic / Kinematic / Static) | `b3Body` |
| Static geometry | any collider without a `Rigidbody` on it or an ancestor | `b3Body` of type static |
| Shapes | `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `MeshCollider` | `b3Shape` (hull, sphere, capsule, mesh) |
| Compound body | colliders on children of an entity with a `Rigidbody` | several shapes on one body |
| Constraint | `PhysicsJoint` | weld / revolute / spherical / prismatic / distance joint |
| Character | `CharacterController` | Box3D mover + a kinematic capsule body |
| Collision callbacks | `CollisionEvent` (`Modules/Dementia/Source/Events/Event.h` event) | contact and sensor begin/end events |

Units are metres, kilograms and seconds. Angles on components are degrees; Box3D gets radians.

## Key Files

| Path | Role |
|------|------|
| `Source/Cores/PhysicsCore.h` / `.cpp` | The core: Box3D world, body / shape / joint sync, stepping, interpolation, events, queries, character mover, job bridge |
| `Source/Components/Physics/Rigidbody.h` | Body settings and the runtime API (forces, velocities, `MoveTo`, `Teleport` (moves the Transform too), sleep) |
| `Source/Components/Physics/Colliders.h` | `ColliderSettings` mixin (centre, trigger, friction, restitution, density) and the four collider components |
| `Source/Components/Physics/CharacterController.h` | Capsule character settings and API (`SetMoveInput`, `Move`, `Jump`, `IsOnGround`); also scriptable from C# (`Docs/Scripting-DotNet.md`) |
| `Source/Components/Physics/PhysicsJoint.h` | Joint type, anchors, axis, limits, motor, spring, break force |
| `Source/Physics/PhysicsTypes.h` | `PhysicsLayers`, `ForceMode`, `RaycastHit`, `CollisionEvent` |
| `Source/Physics/Box3DUtils.h` | Vector / quaternion conversions; packing Box3D ids into `uint64_t` so headers don't include Box3D |
| `Source/Physics/PhysicsDebugDraw.cpp` | `b3DebugDraw` callbacks routed to `DebugDraw` |
| `Source/Engine/ProjectSettings.h` | Layer collision matrix and gravity (the project's `ProjectSettings.json` under `Assets/Config`) |
| `ThirdParty/Box3D.sharpmake.cs` | Box3D build (C17; AVX2 off on Windows) |
| `Source/Cores/Physics2DCore.h` / `.cpp` | The 2D core, the same structure on Box2D; its debug draw is built in |
| `Source/Components/Physics/Rigidbody2D.h`, `Colliders2D.h`, `PhysicsJoint2D.h`, `CharacterController2D.h` | The 2D components |
| `Source/Physics/Box2DUtils.h` / `Source/Physics/PhysicsHash.h` | Box2D conversions (XY plane, angle about Z) / hashing shared by both cores |
| `Tests/Source/PhysicsTests.cpp` / `Tests/Source/Physics2DTests.cpp` | Behavioural tests |

## How It Works

### Frame placement

`PhysicsCore` is created in `Engine::Init` and added to the `World` in `Engine::InitGame` like the other engine-owned cores. The frame loop (`Engine::Run`) drives it twice:

1. **Inside the fixed-step loop**, `PhysicsCore::FixedUpdate` runs after `World::FixedUpdateLoadedCores` and `Game::OnFixedUpdate`. Gameplay applies its forces first, then the world steps. One fixed step is one `b3World_Step` with 4 sub-steps.
2. **Right after the fixed loop** (before scene cores and `Game::OnUpdate`), `PhysicsCore::Update` runs:
   - in play mode it writes interpolated poses, so gameplay and cameras read this frame's pose;
   - in edit mode it syncs bodies to their Transforms.

The simulation only advances between `OnStart` and `OnStop`, which `World::Start` / `World::Stop` call for play mode and game builds. `OnStart` builds every body immediately, so queries work from the first frame of play. The Box3D world lives as long as the core and survives scene loads; bodies of destroyed entities are removed on the next sync.

### Bodies and shapes

`PhysicsCore::SyncBodies` runs before every step and every edit-mode update. It resolves an **owner** for each active collider:

- the collider's own entity if it has an enabled `Rigidbody`;
- otherwise the nearest ancestor with an enabled `Rigidbody` (a compound body);
- otherwise the entity itself, as a static body.

A non-convex `MeshCollider` always owns its body. An entity with a `Rigidbody` and no colliders still gets a body, so joints can attach to it.

Records are keyed by the owner's entity ID. Each record stores two signature hashes:

- **Shape signature:** collider settings, the entity layer and its collision mask, world scales, and the colliders' poses relative to the owner. A change rebuilds every shape on the body.
- **Body signature:** the `Rigidbody` fields. A change only re-applies body settings: type, damping, gravity scale, bullet flag, sleep, motion locks and mass.

Shape sizing:

- **Box:** `Size` × the entity's world scale (per axis).
- **Sphere:** `Radius` × the largest absolute world-scale axis.
- **Capsule:** `Radius` × the larger of the two radial scale axes; `Height` × the scale along `Direction`. Height includes the caps.
- **Mesh, convex** (and any mesh on a dynamic body): a hull of the mesh's vertices, scaled by the world scale. Box3D caps hulls at 128 edges, so the core retries with 40, 28, 16 and then 8 vertices until one fits.
- **Mesh, triangles:** `b3CreateMeshShape` with the world scale. Only on static or kinematic bodies.

Hull and triangle data are cached per `MeshData*` and freed when no body uses them. Mesh colliders read `MeshData::CollisionPositions` / `CollisionIndices`, a copy of the geometry kept when `InitMesh` frees the vertex arrays.

When a collider is added from the editor or code, its `Init` fits it to the entity's mesh bounds. Deserialized colliders keep their saved values.

### Mass

With `Rigidbody::Mass` > 0, the body's mass is set to exactly that value. Box3D's inertia from the shapes is scaled to match, so the distribution is kept. With `Mass` = 0, mass comes from the collider densities.

### Transform sync

| Body | Play mode | Edit mode |
|------|-----------|-----------|
| Dynamic | The body drives the Transform: interpolated between the last two fixed steps when `Interpolate` is on. A Transform changed by anything else is detected and teleports the body. | The body follows the Transform |
| Kinematic | Follows the Transform through `b3Body_SetTargetTransform` (smooth, pushes dynamic bodies). `Rigidbody::MoveTo` sets both the target and the Transform. | Follows the Transform |
| Static | A moved Transform teleports the body | Follows the Transform |

External moves are detected by comparing each Transform against the pose the core last wrote or applied (`BodyRecord::AppliedPosition` / `AppliedRotation`). This is how editor gizmos, `Transform::SetPosition` from gameplay, and prefab or undo restores all move bodies.

### Layers and filtering

A shape's `filter.categoryBits` is `1 << entity layer`, and its `maskBits` is `ProjectSettings::GetCollisionMask(layer)`. `ProjectSettings::SetLayersCollide` keeps the matrix symmetric. Changing the matrix changes the shape signature, so it takes effect on the next sync.

Every query takes a layer mask (`PhysicsLayers::Bit(n)` / `PhysicsLayers::All`) and tests it against the shapes' category bits.

### Events

After each step, `ProcessEvents` reads Box3D's contact begin/end events and sensor begin/end events, and fires a `CollisionEvent` for each one:

- `A` and `B` are the **collider** entities. For triggers, `A` is the trigger.
- `IsTrigger` and `State` (Enter / Exit) are set on every event.
- `Point` and `Normal` are filled for contact Enter events.

All events of a step are collected first and fired afterwards, so handlers may change the world. Every shape enables sensor and contact events. Joints whose break force was exceeded are destroyed from Box3D's joint events, and `PhysicsJoint` remembers that it broke.

### Queries

All queries are on `PhysicsCore` and skip triggers:

| Query | Result |
|-------|--------|
| `Raycast( origin, direction, maxDistance, hit, mask )` | closest hit |
| `Linecast( start, end, hit, mask )` | closest hit between two points |
| `RaycastAll( ... )` | every hit, sorted by distance |
| `SphereCast( origin, radius, direction, maxDistance, hit, mask )` | closest hit of a swept sphere |
| `OverlapSphere` / `OverlapBox` | entities overlapping the shape (deduplicated) |

`RaycastHit` carries the collider's `Entity`, `Position`, `Normal`, `Distance` and `Fraction`. The 3-argument `Raycast( start, end, hit )` is kept for older game code; it is a `Linecast`.

### Character controller

`StepCharacters` runs before the world step, and does this each step for every enabled `CharacterController`:

1. **Steer.** Horizontal velocity moves toward `SetMoveInput × MaxSpeed`, limited by `Acceleration`; airborne it uses `AirControl` × `Acceleration`.
2. **Gravity and jumping.** Gravity × `GravityScale` is applied. `Jump` while grounded sets the vertical speed to `sqrt(2 g JumpHeight)`.
3. **Move.** Up to 5 iterations of: `b3World_CollideMover` → `b3SolvePlanes` → `b3World_CastMover`. Its own capsule is excluded by a filter callback.
4. **Settle.** Contact planes at the final position decide grounding: a normal with `y >= cos(SlopeLimit)`. They also clip the velocity. If the character just walked off an edge, a `GroundSnap` downward cast keeps it on the ground.
5. **Push.** Dynamic bodies touched get an impulse along the intended horizontal velocity × `PushStrength`.

The capsule is always upright and the Transform's rotation is left alone. The Transform sits at the capsule centre plus `Center`, interpolated like dynamic bodies. A Transform moved by anything else is treated as a teleport. A kinematic capsule body follows the character, so rigidbodies collide with it and queries hit it.

### Joints

`SyncJoints` creates joints from `PhysicsJoint` components:

- **Bodies.** Body A is `ConnectedBody`'s body, or a shared static ground body when `ConnectedBody` is empty; body B is the joint entity's body. Box3D measures B relative to A, so a positive `MotorSpeed`, translation or limit describes the entity's own motion, right-handed about `Axis`.
- **Joint frame.** Placed at `Anchor` and rotated so the joint's axis matches `Axis`, both in the entity's local space. Revolute (Hinge) joints turn about the frame's z axis; prismatic (Slider) joints slide along its x axis.
- **Fixed, Hinge, BallSocket, Slider:** the bodies are joined where the anchor is when the joint is created.
- **Distance:** joins `Anchor` to `ConnectedAnchor`, which is local to the connected body, or a world point for the ground. `Distance` = 0 keeps the distance at creation.
- **Limits:** degrees for hinges, the cone half-angle (`UpperLimit`) for ball sockets, metres for sliders and distance joints.
- **Motors:** deg/s for hinges, m/s for sliders.
- **Breaking:** `BreakForce` > 0 sets the force and torque thresholds. Box3D reports a joint event once a threshold is exceeded, and the core destroys the joint.

Joints have their own signature and are re-created when it changes.

### Threading

`b3WorldDef::enqueueTask` / `finishTask` map to `Jobs::JobSystem::Submit` / `Wait`. The task slots live in `PhysicsCore::m_tasks`, an array of 128. Past that, tasks run inline. `workerCount` is the job system's thread count. Everything else in the core runs on the main thread.

### Debug drawing

- **Whole world:** `PhysicsCore::DebugDrawEnabled` (Scene View > View > Physics, the `View.Physics` action, or the core's inspector) draws every shape, joint and contact through `b3World_Draw`. `PhysicsDebugDraw` turns Box3D's debug shapes into wireframes: hull edges, mesh triangles up to 40000, spheres and capsules.
- **Selection:** the editor draws gizmos for the selected entity's colliders (green, or blue for triggers), its character capsule, and its joint anchor, axis and connection (`SceneTools::DrawPhysicsGizmos`).

### Old scenes

`SceneSerializer::UpgradeComponent` converts the Bullet-era `Rigidbody` (identified by its `ColliderType` key):

- **Shape:** a `BoxCollider` whose `Size` is `2 × Scale / Transform scale`, since the old box used `Scale` as world half-extents. Old sphere bodies get a `SphereCollider`.
- **Mass:** with mass > 0 the collider comes with a `Rigidbody`; with mass 0 it stands alone as a static collider.
- **Character:** a Bullet-era `CharacterController` (identified by its `JumpForce` key) becomes a default one.

### 2D physics (Box2D)

`Physics2DCore` sits next to `PhysicsCore` in the frame loop and runs the same pipeline: body ownership, shape and body signatures, fixed step plus interpolation, edit-mode sync, events and queries. Where it differs:

- **The plane.** A body owns its Transform's X/Y position and its angle about world Z (`Box2DUtils::AngleOf`, the ZYX yaw). Poses are written with `WithAngle`, which turns the current rotation about world Z by the angle change. A world-Z pre-rotation adds exactly to that yaw, so the Transform's Z position and any X/Y tilt survive: a cylinder turned on its side rolls like a wheel.
- **Shapes.** Shapes come from the entity's local XY plane, scaled by its X/Y world scale.
  - `BoxCollider2D`: a rounded box (`EdgeRadius`).
  - `CircleCollider2D`: `Radius` × the larger scale axis.
  - `CapsuleCollider2D`: vertical or horizontal; it collapses to a circle when it's shorter than its diameter.
  - `PolygonCollider2D`: the convex hull of `Points`, reduced to Box2D's 8-vertex limit by dropping the vertex that removes the least area (`Collider2DUtils::ConvexHull`).
  - `EdgeCollider2D`: two-sided segments along `Points` (optionally `Loop`ed). They have no area.
- **Joints** (`PhysicsJoint2D`). Fixed (weld), Hinge (revolute), Slider (prismatic), Distance and Wheel, with the same A = connected / B = entity convention.
  - Wheel joints go on the wheel, connected to the chassis.
  - Box2D v3.1 has no joint events, so `BreakJoints` compares `b2Joint_GetConstraintForce` / `Torque` against `BreakForce` after each step.
- **`CharacterController2D`.** The same mover loop on `b2World_CollideMover` / `b2SolvePlanes` / `b2World_CastMover`, plus `CoyoteTime`: a jump still works for a moment after leaving a ledge. The input is a horizontal value in [-1, 1], and only gravity's Y applies.
  - Box2D's mover queries take no filter callback. Each character therefore gets a unique high category bit (`m_selfBit`, bits 32-63), and its query uses that bit as its category.
  - Every shape's mask carries all of those bits except its own character's, so a character's queries skip only its own capsule. The low 32 bits stay the layer matrix.
- **Tasks.** Box2D asks for parallel-for ranges. `EnqueueTask` splits them across the job system, and the worker index Box2D expects is `Jobs::JobSystem::GetCurrentThreadIndex` (0 on the stepping thread).
- **Queries.** `Raycast` / `RaycastAll` / `Linecast` / `CircleCast` / `OverlapCircle` / `OverlapBox` (with an angle) / `OverlapPoint`. `RaycastHit` positions take the hit entity's Z.
- **Debug draw.** It shares `PhysicsCore::DebugDrawEnabled` and is drawn flat at z = 0.

## How to Extend

- **React to collisions:** register an `EventReceiver` for `CollisionEvent::GetEventId()` and compare `A` / `B` against your entities. See `PhysicsTest::CollisionLog` in the tests.
- **A new collider shape:**
  1. Add a component that mixes in `ColliderSettings`, and register it with `ME_COLLIDER_FIELDS()`.
  2. Add it to `PhysicsCore`'s filter (`RequiresOneOf`) and to `HasAnyCollider`.
  3. Hash it in `ComputeShapeSignature`.
  4. Create its `b3Shape` in `BuildShapes`.
  5. Draw it in `SceneTools::DrawPhysicsGizmos`.
- **A new joint type:**
  1. Add the enum value and its reflection entry.
  2. Add a `case` in `SyncJoints` that copies the shared definition with `ApplyJointBase`, which keeps Box3D's validation cookie.

## Caveats & Fragility

- **Transforms hold the interpolated pose.** Reading a dynamic body's Transform during `FixedUpdate` gives the last rendered pose, not the body's. Use `Rigidbody::GetVelocity` and the other `Rigidbody` methods for exact values.
- **A Transform moved by code teleports the body and keeps its velocity.** Moving platforms belong on kinematic bodies, which sweep.
- **Static bodies moved in play mode teleport** and won't carry or push anything.
- **Sphere and capsule colliders don't support non-uniform scale**; they take the largest relevant axis.
- **The hull cache is keyed by `MeshData*`.** If a mesh is freed and another is allocated at the same address with the same component pointer, the stale hull is kept until the collider's shape signature changes.
- **`CollisionEvent` is global.** Every receiver sees every contact and filters by entity.
- **Gravity:** edit mode resets the world's gravity to Project Settings every frame. `PhysicsCore::SetGravity` during play lasts until the next stop.
- **Triangle mesh colliders on dynamic bodies silently become hulls.**
- **Box3D is pre-1.0** (a pinned `ThirdParty/box3d` submodule commit). Its API is still moving, and an update may need `Box3DUtils` / `PhysicsCore` changes.
- **2D and 3D bodies live in separate worlds and never touch.** An entity with both a `Rigidbody` and a `Rigidbody2D` would have its pose written by both cores; don't mix them on one entity.
- **2D debug drawing is flattened to z = 0**, regardless of the bodies' Transform Z.
- **2D edge colliders have no area.** Two edges never collide, so they only work as static or kinematic level geometry.
- **Up to 32 characters per 2D world get a unique self-filter bit.** The 33rd shares a bit with the first, and those two pass through each other's capsules.

## Related Docs

- [Architecture.md](Architecture.md): the frame loop and engine-owned cores.
- [Cores-and-Components-Reference.md](Cores-and-Components-Reference.md): the catalog of cores and components.
- [Jobs-and-Events.md](Jobs-and-Events.md): the job system the solver runs on, and event delivery.
- [Editor-Havana.md](Editor-Havana.md): gizmos, the Create menu and Project Settings.
- [Serialization-and-Scenes.md](Serialization-and-Scenes.md): component migration on load.
