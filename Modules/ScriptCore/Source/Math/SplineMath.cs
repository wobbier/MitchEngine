using System;
using System.Collections.Generic;

namespace ScriptCore;

/// <summary>
/// GAME-2: the Spline component's curve math (C++ SplineMath in Components/Gameplay/Spline.cpp,
/// kept in step): a uniform Catmull-Rom curve through every point. Open curves extrapolate their end
/// tangents, closed ones wrap; t runs 0..1 over the whole curve, each segment an equal share.
/// </summary>
public static class SplineMath
{
    /// <summary>Samples per segment for arc length.</summary>
    public const int SamplesPerSegment = 16;

    /// <summary>How many segments a curve through that many points has.</summary>
    public static int GetSegmentCount(int pointCount, bool closed) => pointCount < 2 ? 0 : closed ? pointCount : pointCount - 1;

    /// <summary>The Catmull-Rom curve through points at t (0..1 along it).</summary>
    public static Vector3 Evaluate(IReadOnlyList<Vector3> points, bool closed, float t)
    {
        if (points.Count == 0) return Vector3.Zero;
        if (points.Count == 1) return points[0];
        int segment = Locate(points.Count, closed, t, out float f);
        Controls(points, closed, segment, out var p0, out var p1, out var p2, out var p3);
        float f2 = f * f, f3 = f2 * f;
        return (p1 * 2f + (p2 - p0) * f + (p0 * 2f - p1 * 5f + p2 * 4f - p3) * f2 + (p1 * 3f - p0 - p2 * 3f + p3) * f3) * 0.5f;
    }

    /// <summary>
    /// dP/dt (t over the whole curve).
    /// </summary>
    public static Vector3 Derivative(IReadOnlyList<Vector3> points, bool closed, float t)
    {
        if (points.Count < 2) return Vector3.Zero;
        int segment = Locate(points.Count, closed, t, out float f);
        Controls(points, closed, segment, out var p0, out var p1, out var p2, out var p3);
        var perSegment = ((p2 - p0) + (p0 * 2f - p1 * 5f + p2 * 4f - p3) * (2f * f) + (p1 * 3f - p0 - p2 * 3f + p3) * (3f * f * f)) * 0.5f;
        return perSegment * GetSegmentCount(points.Count, closed);
    }

    private static int Locate(int count, bool closed, float t, out float f)
    {
        int segments = GetSegmentCount(count, closed);
        float scaled = Math.Clamp(t, 0f, 1f) * segments;
        int segment = Math.Min((int)MathF.Floor(scaled), segments - 1);
        f = scaled - segment;
        return segment;
    }

    private static void Controls(IReadOnlyList<Vector3> points, bool closed, int segment, out Vector3 p0, out Vector3 p1, out Vector3 p2, out Vector3 p3)
    {
        Vector3 At(int i)
        {
            int n = points.Count;
            if (closed) return points[((i % n) + n) % n];
            if (i < 0) return points[0] * 2f - points[1];
            if (i >= n) return points[n - 1] * 2f - points[n - 2];
            return points[i];
        }
        p0 = At(segment - 1); p1 = At(segment); p2 = At(segment + 1); p3 = At(segment + 2);
    }
}

/// <summary>
/// A spline's curve in world space at one moment: build it once (Spline.GetPath()) and ask it
/// several questions, rather than reading the component for each.
/// </summary>
public sealed class SplinePath
{
    private readonly Vector3[] _points;
    private readonly bool _closed;
    private float[]? _lengths;

    /// <summary>A curve through world points (open or closed).</summary>
    public SplinePath(IReadOnlyList<Vector3> worldPoints, bool closed)
    {
        _points = new Vector3[worldPoints.Count];
        for (int i = 0; i < _points.Length; ++i) _points[i] = worldPoints[i];
        _closed = closed;
    }

    /// <summary>Its world points.</summary>
    public IReadOnlyList<Vector3> Points => _points;
    /// <summary>Whether it loops.</summary>
    public bool Closed => _closed;

    /// <summary>The world point at t (0..1).</summary>
    public Vector3 GetPoint(float t) => SplineMath.Evaluate(_points, _closed, t);
    /// <summary>The direction of travel at t.</summary>
    public Vector3 GetTangent(float t) => SplineMath.Derivative(_points, _closed, t).Normalized();

    /// <summary>The curve's length.</summary>
    public float Length => Lengths()[^1];

    /// <summary>
    /// The t a distance along the curve reaches (closed curves wrap; open ones clamp).
    /// </summary>
    public float GetTAtDistance(float distance)
    {
        float[] table = Lengths();
        float length = table[^1];
        if (length <= 0f) return 0f;
        distance = _closed ? ((distance % length) + length) % length : Math.Clamp(distance, 0f, length);
        int index = Array.BinarySearch(table, distance);
        if (index >= 0) return (float)index / (table.Length - 1);
        index = ~index;
        if (index <= 0) return 0f;
        if (index >= table.Length) return 1f;
        float span = table[index] - table[index - 1];
        float within = span > 0f ? (distance - table[index - 1]) / span : 0f;
        return (index - 1 + within) / (table.Length - 1);
    }

    /// <summary>The point distance along the curve.</summary>
    public Vector3 GetPointAtDistance(float distance) => GetPoint(GetTAtDistance(distance));

    /// <summary>
    /// The t of the curve's point nearest a world position.
    /// </summary>
    public float GetNearestT(Vector3 world)
    {
        int samples = Math.Max(SplineMath.GetSegmentCount(_points.Length, _closed), 1) * SplineMath.SamplesPerSegment;
        float best = 0f, bestDistance = (GetPoint(0f) - world).LengthSquared();
        for (int i = 1; i <= samples; ++i)
        {
            float t = (float)i / samples, d = (GetPoint(t) - world).LengthSquared();
            if (d < bestDistance) { bestDistance = d; best = t; }
        }
        float low = Math.Max(best - 1f / samples, 0f), high = Math.Min(best + 1f / samples, 1f);
        for (int i = 0; i < 24; ++i)
        {
            float a = low + (high - low) / 3f, b = high - (high - low) / 3f;
            if ((GetPoint(a) - world).LengthSquared() < (GetPoint(b) - world).LengthSquared()) high = b; else low = a;
        }
        return (low + high) * 0.5f;
    }

    private float[] Lengths()
    {
        if (_lengths != null) return _lengths;
        int samples = Math.Max(SplineMath.GetSegmentCount(_points.Length, _closed), 1) * SplineMath.SamplesPerSegment;
        var table = new float[samples + 1];
        var previous = GetPoint(0f);
        for (int i = 1; i <= samples; ++i)
        {
            var point = GetPoint((float)i / samples);
            table[i] = table[i - 1] + (point - previous).Length();
            previous = point;
        }
        return _lengths = table;
    }
}
