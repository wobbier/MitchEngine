using System;

namespace ScriptCore;

/// <summary>
/// stateless proxy, just marshalling really
/// </summary>
public abstract class Component
{
    /// <summary>The entity this component is on.</summary>
    public Entity Entity { get; internal set; }

    /// <summary>
    /// if the entity is being observed by the world in any capacity, you cannot hide in silence
    /// </summary>
    public bool Exists => Entity.IsAlive && Entity.HasComponentNamed(GetType().Name);

    /// <summary>The entity's Transform.</summary>
    public Transform transform => Entity.GetComponent<Transform>();
    /// <summary>The entity's component of type T, or null.</summary>
    public T GetComponent<T>() where T : Component, new() => Entity.GetComponent<T>();
    /// <summary>Whether the entity has a component of type T.</summary>
    public bool HasComponent<T>() where T : Component, new() => Entity.HasComponent<T>();
    /// <summary>Adds a component of type T to the entity and returns it.</summary>
    public T AddComponent<T>() where T : Component, new() => Entity.AddComponent<T>();

    /// <summary>Same component on the same entity; two missing components are equal (Unity-style fake null).</summary>
    public bool Equals(Component other)
    {
        bool meNull = !Exists;
        bool otherNull = other is null || !other.Exists;
        if (meNull || otherNull) return meNull && otherNull;
        return Entity == other.Entity && GetType() == other.GetType();
    }

    /// <summary>Same component on the same entity.</summary>
    public override bool Equals(object obj) => Equals(obj as Component);
    /// <summary>A hash of the type and entity.</summary>
    public override int GetHashCode() => HashCode.Combine(GetType(), Entity.Index, Entity.Generation);

    /// <summary>Same component, or both missing: a removed component compares equal to null.</summary>
    public static bool operator ==(Component a, Component b)
    {
        bool aNull = a is null || !a.Exists;
        bool bNull = b is null || !b.Exists;
        if (aNull || bNull) return aNull && bNull;
        return a.Entity == b.Entity && a.GetType() == b.GetType();
    }

    /// <summary>Not the same component.</summary>
    public static bool operator !=(Component a, Component b) => !(a == b);
}
