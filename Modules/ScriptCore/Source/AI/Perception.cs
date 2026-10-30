using System;
using System.Collections.Generic;

namespace ScriptCore.AI;

// What an AI noticed: sight (range, field of view, line of sight through physics) and hearing
// (Noise.Make), with a memory of where it last saw or heard its target.
public sealed class Perception
{
    public float SightRange = 12f;
    public float FieldOfView = 120f;   // degrees, full cone
    public float EyeHeight = 0.6f;     // above the entity's position
    public float HearingScale = 1f;    // multiplies each noise's radius
    public float MemorySeconds = 6f;

    public bool CanSee { get; private set; }
    public bool HasLastKnown => _lastKnownAge <= MemorySeconds && _hasLastKnown;
    public Vector3 LastKnownPosition { get; private set; }
    public float LastKnownAge => _lastKnownAge;
    public bool HeardSomething { get; private set; }

    private bool _hasLastKnown;
    private float _lastKnownAge = float.MaxValue;
    private int _lastNoise;

    // Looks for target from self and listens for noises; call once per update.
    public void Update(Entity self, Entity target, float deltaTime)
    {
        _lastKnownAge += deltaTime;
        HeardSomething = false;
        var transform = self.Transform;
        var eye = transform.WorldPosition + Vector3.Up * EyeHeight;
        CanSee = target.IsAlive && Sees(eye, transform.Forward, target);
        if (CanSee)
        {
            Remember(target.Transform.WorldPosition);
        }
        foreach (var noise in Noise.Since(_lastNoise))
        {
            if (Vector3.Distance(noise.Position, eye) <= noise.Radius * HearingScale && noise.Source != self)
            {
                HeardSomething = true;
                if (!CanSee) Remember(noise.Position);
            }
        }
        _lastNoise = Noise.Count;
    }

    public void Forget()
    {
        _hasLastKnown = false;
        _lastKnownAge = float.MaxValue;
    }

    private void Remember(Vector3 position)
    {
        LastKnownPosition = position;
        _lastKnownAge = 0f;
        _hasLastKnown = true;
    }

    private bool Sees(Vector3 eye, Vector3 forward, Entity target)
    {
        var at = target.Transform.WorldPosition;
        if (!InCone(eye, forward, at, SightRange, FieldOfView)) return false;
        var toTarget = at - eye;
        float distance = toTarget.Length();
        // Blocked when something other than the target is hit first.
        if (Physics.Raycast(eye, toTarget / distance, distance, out var hit) && hit.Entity != target && hit.Distance < distance - 0.25f)
        {
            return false;
        }
        return true;
    }

    // Within range and inside the view cone (ignoring height), from an eye looking along forward.
    public static bool InCone(Vector3 eye, Vector3 forward, Vector3 point, float range, float fieldOfView)
    {
        var toPoint = point - eye;
        if (toPoint.Length() > range) return false;
        var flat = new Vector3(toPoint.x, 0f, toPoint.z);
        var look = new Vector3(forward.x, 0f, forward.z);
        if (flat.LengthSquared() < 1e-6f || look.LengthSquared() < 1e-6f) return true;
        float cos = Vector3.Dot(flat.Normalized(), look.Normalized());
        return cos >= MathF.Cos(fieldOfView * 0.5f * Mathf.Deg2Rad);
    }
}

// Sounds AIs can hear: Noise.Make(where, radius) from gunshots, footsteps, a thrown bottle.
public static class Noise
{
    public readonly record struct Event(Vector3 Position, float Radius, Entity Source, int Index);
    private static readonly List<Event> Recent = new();
    private static int _count;

    // How many noises have ever been made (perception remembers where it read up to).
    public static int Count => _count;

    public static void Make(Vector3 position, float radius, Entity source = default)
    {
        Recent.Add(new Event(position, radius, source, _count++));
        if (Recent.Count > 64) Recent.RemoveAt(0);
    }

    public static IEnumerable<Event> Since(int index)
    {
        foreach (var e in Recent)
        {
            if (e.Index >= index) yield return e;
        }
    }
}
