using System;

namespace ScriptCore;

// stateless proxy, just marshalling really
public abstract class Component
{
    public Entity Entity { get; internal set; }

    // if the entity is being observed by the world in any capacity, you cannot hide in silence
    public bool Exists => Entity.IsAlive && Entity.HasComponentNamed(GetType().Name);

    public Transform transform => Entity.GetComponent<Transform>();
    public T GetComponent<T>() where T : Component, new() => Entity.GetComponent<T>();
    public bool HasComponent<T>() where T : Component, new() => Entity.HasComponent<T>();
    public T AddComponent<T>() where T : Component, new() => Entity.AddComponent<T>();

    public bool Equals(Component other)
    {
        bool meNull = !Exists;
        bool otherNull = other is null || !other.Exists;
        if (meNull || otherNull) return meNull && otherNull;
        return Entity == other.Entity && GetType() == other.GetType();
    }

    public override bool Equals(object obj) => Equals(obj as Component);
    public override int GetHashCode() => HashCode.Combine(GetType(), Entity.Index, Entity.Counter);

    public static bool operator ==(Component a, Component b)
    {
        bool aNull = a is null || !a.Exists;
        bool bNull = b is null || !b.Exists;
        if (aNull || bNull) return aNull && bNull;
        return a.Entity == b.Entity && a.GetType() == b.GetType();
    }

    public static bool operator !=(Component a, Component b) => !(a == b);
}
