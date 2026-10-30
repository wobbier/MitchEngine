using System;

namespace ScriptCore;

/// <summary>The entity's Spline (GAME-2): a curve through local points, for camera paths, rails, roads and patrols. Queries are in world space; for several in a frame, take GetPath() once: var path = GetComponent&lt;Spline&gt;().GetPath(); transform.WorldPosition = path.GetPointAtDistance(travelled);</summary>
public class Spline : Component
{
    /// <summary>
    /// Local positions the curve passes through, in order.
    /// </summary>
    public Vector3[] Points
    {
        get => Entity.GetField("Spline", "Points", Array.Empty<Vector3>());
        set => Entity.SetField("Spline", "Points", value ?? Array.Empty<Vector3>());
    }

    /// <summary>Whether the curve loops back to its first point.</summary>
    public bool Closed
    {
        get => Entity.GetField("Spline", "Closed", false);
        set => Entity.SetField("Spline", "Closed", value);
    }

    /// <summary>
    /// The curve now, in world space. Points go through the entity's world position, rotation and
    /// scale (the product of the local scales up the hierarchy).
    /// </summary>
    public SplinePath GetPath()
    {
        var points = Points;
        var t = transform;
        var origin = t.WorldPosition;
        var right = t.Right;
        var up = t.Up;
        var forward = t.Forward;
        var scale = Vector3.One;
        int depth = 0;
        for (Entity e = Entity; e.IsAlive && depth < 64; e = e.Transform.Parent, ++depth)
        {
            var s = e.Transform.Scale;
            scale = new Vector3(scale.x * s.x, scale.y * s.y, scale.z * s.z);
        }
        var world = new Vector3[points.Length];
        for (int i = 0; i < points.Length; ++i)
        {
            var p = points[i];
            world[i] = origin + right * (p.x * scale.x) + up * (p.y * scale.y) + forward * (p.z * scale.z);
        }
        return new SplinePath(world, Closed);
    }

    /// <summary>The world point at t (0..1 along the curve).</summary>
    public Vector3 GetPoint(float t) => GetPath().GetPoint(t);
    /// <summary>The world direction of travel at t.</summary>
    public Vector3 GetTangent(float t) => GetPath().GetTangent(t);
    /// <summary>The curve's world length.</summary>
    public float Length => GetPath().Length;
    /// <summary>The t that lies distance along the curve.</summary>
    public float GetTAtDistance(float distance) => GetPath().GetTAtDistance(distance);
    /// <summary>The t nearest a world point.</summary>
    public float GetNearestT(Vector3 world) => GetPath().GetNearestT(world);
}
