namespace ScriptCore;

/// <summary>
/// A shape lag-compensated shots test against (NET-8; ME-312 wrapper): the server keeps where each one
/// was for the last ~400 ms and rewinds to what the shooter saw (Net.LagCompensatedRaycast). Hitboxes
/// aren't colliders. One per entity: a pawn's head is a child entity with its own. Enabled = false
/// takes it out of shots (a dead pawn), from the server's next recorded tick.
/// </summary>
public class NetHitbox : Component
{
    /// <summary>A capsule along the entity's local Y, or an oriented box.</summary>
    public NetHitboxShape Shape
    {
        get => System.Enum.TryParse(Entity.GetField<string>("NetHitbox", "Shape", "Capsule"), out NetHitboxShape shape) ? shape : NetHitboxShape.Capsule;
        set => Entity.SetField("NetHitbox", "Shape", value.ToString());
    }

    /// <summary>The shape's centre in the entity's local space.</summary>
    public Vector3 Center
    {
        get => Entity.GetField<Vector3>("NetHitbox", "Center", Vector3.Zero);
        set => Entity.SetField("NetHitbox", "Center", value);
    }

    /// <summary>Capsule: its radius in metres.</summary>
    public float Radius
    {
        get => Entity.GetField<float>("NetHitbox", "Radius", 0.4f);
        set => Entity.SetField("NetHitbox", "Radius", value);
    }

    /// <summary>Capsule: its height end to end, caps included, in metres.</summary>
    public float Height
    {
        get => Entity.GetField<float>("NetHitbox", "Height", 1.8f);
        set => Entity.SetField("NetHitbox", "Height", value);
    }

    /// <summary>Box: half its size on each local axis, in metres.</summary>
    public Vector3 HalfExtents
    {
        get => Entity.GetField<Vector3>("NetHitbox", "HalfExtents", new Vector3(0.5f, 0.5f, 0.5f));
        set => Entity.SetField("NetHitbox", "HalfExtents", value);
    }

    /// <summary>
    /// A moving blocker (a door, a platform), not a target (NET-15): rewound shots stop where it was
    /// in the shooter's view, never hit it, and ignore this entity's present-day colliders.
    /// </summary>
    public bool Occluder
    {
        get => Entity.GetField<bool>("NetHitbox", "Occluder", false);
        set => Entity.SetField("NetHitbox", "Occluder", value);
    }
}

/// <summary>A NetHitbox's shape.</summary>
public enum NetHitboxShape
{
    /// <summary>Along the entity's local Y: Height end to end, Radius.</summary>
    Capsule,
    /// <summary>An oriented box: HalfExtents.</summary>
    Box,
}
