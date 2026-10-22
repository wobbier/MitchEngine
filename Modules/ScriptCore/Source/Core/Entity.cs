using System;
using System.Runtime.InteropServices;
using System.Text;

namespace ScriptCore;

// struct because it's mapped to the engine EntityID struct. don't fuckin add anything to this <3 or at least update it to reflect it
[StructLayout(LayoutKind.Sequential)]
public readonly struct Entity : IEquatable<Entity>
{
    public static readonly Entity Null = default;   // {0, 0}

    public readonly ulong Index;
    public readonly ulong Counter;

    public Entity(ulong inIndex, ulong inCounter)
    {
        Index = inIndex;
        Counter = inCounter;
    }

    public bool IsAlive { get { unsafe { return Engine._api.Entity_IsAlive(this) != 0; } } }

    // what is life?
    public static implicit operator bool(Entity e) => e.IsAlive;

    public bool HasComponent<T>() where T : Component, new()
    {
        unsafe { fixed (byte* p = ComponentName<T>.Utf8) return Engine._api.Entity_HasComponent(this, p) != 0; }
    }

    public T GetComponent<T>() where T : Component, new()
        => HasComponent<T>() ? new T { Entity = this } : null;

    public bool TryGetComponent<T>(out T component) where T : Component, new()
    {
        if (HasComponent<T>())
        {
            component = new T { Entity = this };
            return true;
        }
        component = null;
        return false;
    }

    public T AddComponent<T>() where T : Component, new()
    {
        unsafe { fixed (byte* p = ComponentName<T>.Utf8) Engine._api.Entity_AddComponent(this, p); }
        return GetComponent<T>();
    }

    public Transform Transform => GetComponent<Transform>();

    // used by Component's fake-null check, where the type is only known at runtime.
    internal bool HasComponentNamed(string inName)
    {
        var bytes = Encoding.UTF8.GetBytes(inName + "\0");
        unsafe { fixed (byte* p = bytes) return Engine._api.Entity_HasComponent(this, p) != 0; }
    }

    public bool Equals(Entity o) => Index == o.Index && Counter == o.Counter;
    public override bool Equals(object? obj) => obj is Entity e && Equals(e);
    public override int GetHashCode() => HashCode.Combine(Index, Counter);
    public static bool operator ==(Entity a, Entity b) => a.Equals(b);
    public static bool operator !=(Entity a, Entity b) => !a.Equals(b);

    public override string ToString() => $"Entity({Index}:{Counter})";
}

// pre-cache per typename so HasComponent/GetComponent doesn't re-marshal the string on every single call.
internal static class ComponentName<T>
{
    public static readonly byte[] Utf8 = Encoding.UTF8.GetBytes(typeof(T).Name + "\0");
}
