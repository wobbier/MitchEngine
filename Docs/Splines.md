# Splines

`Spline` (`Source/Components/Gameplay/Spline.h`, GAME-2) is a curve through points in the entity's local space. Use it for camera paths, rails, roads and patrols.

## The curve

- **Shape:** a uniform Catmull-Rom curve that passes through every point.
- **Open curves** extrapolate their end tangents: the point before the first mirrors the second.
- **`Closed`** joins the last point back to the first.
- **`t`** runs from 0 to 1 over the whole curve, and each segment gets an equal share. Use the distance lookups when speed should be constant.

The math lives in two places, and both are held to the same numbers:

| Side | Code | Tests |
|------|------|-------|
| C++ | `SplineMath` | doctest `SplineTests.cpp` |
| C# | `ScriptCore/Source/Math/SplineMath.cs` | `SplineTests.cs` |

Keep them in step when either changes.

## C++ queries (world space)

- `GetPoint(t)`
- `GetTangent(t)` (normalized)
- `GetLength()`
- `GetTAtDistance(distance)`: closed curves wrap, open ones clamp.
- `GetNearestT(position)`
- `GetLocalPoint(t)`, `GetWorldControlPoint(i)`

Lengths come from 16 samples per segment.

## Editing

Select an entity with a Spline and the World view draws its curve with a numbered handle on each point (`Modules/Havana/Source/Editor/SplineGizmo.*`).

- **Dragging a handle** moves the point across the plane through it that faces the camera, as one undo step.
- **Adding and removing points** is done in the inspector's `Points` list.

## C#

```csharp
var spline = GetComponent<Spline>();
SplinePath path = spline.GetPath();          // world space, once per frame
_travelled += Speed * dt;
transform.WorldPosition = path.GetPointAtDistance(_travelled);
transform.LookDirection(path.GetTangent(path.GetTAtDistance(_travelled)), Vector3.Up);
```

- `Spline.Points` and `Spline.Closed` read and write the component, through `Component_GetField` / `SetField`.
- `Spline.GetPath()` builds a `SplinePath` with these queries: `GetPoint`, `GetTangent`, `Length`, `GetTAtDistance`, `GetPointAtDistance` and `GetNearestT`.
- The convenience methods on `Spline` (`GetPoint(t)`, `Length`, ...) each build a fresh path.
- World points go through the entity's world position and rotation. Scale is the product of the local scales up the hierarchy, which is exact unless a rotated parent scales non-uniformly.

## Tests

- **`SplineFlows.edscript`** (on ScriptTest):
  - Checks the world curve from C++ (`assert-spline`) and from C#, through `Assets/Scripts/Tests/SplineProbe.cs` and the console (`spline-check`).
  - Covers a moved entity, a rotated and scaled one, and a closed loop.
  - Then drags point 1's handle (`spline-handle-mouse`) and undoes and redoes the drag.
- **edscript commands:**
  - `assert-spline Entity | t | x y z`
  - `assert-spline Entity | point i | x y z` (or `!= x y z`)
  - `assert-spline Entity | length | L`
  - `spline-handle-mouse i`
