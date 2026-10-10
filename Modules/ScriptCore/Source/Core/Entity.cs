using System;
using System.Runtime.InteropServices;
using System.Text;

namespace ScriptCore;

// struct because it's mapped to the engine EntityID struct. don't fuckin add anything to this <3 or at least update it to reflect it
[StructLayout(LayoutKind.Sequential)]
public readonly struct Entity : IEquatable<Entity>
{
    public static readonly Entity Null = default;   // {0, 0}

    // Mirrors the engine's 8-byte EntityID: slot index + generation (0 = null).
    public readonly uint Index;
    public readonly uint Generation;

    public Entity(uint inIndex, uint inGeneration)
    {
        Index = inIndex;
        Generation = inGeneration;
    }

    public bool IsAlive { get { unsafe { return Engine._api.Entity_IsAlive(this) != 0; } } }

    // Destroys the entity and its children at the end of the frame.
    public void Destroy()
    {
        unsafe { Engine._api.Entity_Destroy(this); }
    }

    public bool Active
    {
        get { unsafe { return Engine._api.Entity_IsActive(this) != 0; } }
        set { unsafe { Engine._api.Entity_SetActive(this, value ? (byte)1 : (byte)0); } }
    }

    public string Name
    {
        get
        {
            unsafe
            {
                int length = Engine._api.Entity_GetName(this, null, 0);
                var buffer = new byte[length + 1];
                fixed (byte* p = buffer) Engine._api.Entity_GetName(this, p, buffer.Length);
                return Encoding.UTF8.GetString(buffer, 0, length);
            }
        }
        set { unsafe { fixed (byte* p = Engine.Utf8(value)) Engine._api.Entity_SetName(this, p); } }
    }

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

    // Any field of any component by name, including ones without a C# wrapper:
    //   GetField<float>("Light", "Intensity"); SetField("Light", "Color", new Vector3(1, 0.5f, 0));
    // Paths can go deeper ("Settings.Radius", "Color.0"). Missing components or fields return the
    // fallback / false.
    public T GetField<T>(string component, string path, T fallback = default!)
    {
        string? json = GetFieldJson(component, path);
        if (json == null)
        {
            return fallback;
        }
        try { return FieldJson.Read<T>(json); }
        catch (System.Text.Json.JsonException) { return fallback; }
    }

    public bool SetField<T>(string component, string path, T value) => SetFieldJson(component, path, FieldJson.Write(value));

    public string? GetFieldJson(string component, string path)
    {
        var componentBytes = Encoding.UTF8.GetBytes(component + "\0");
        var pathBytes = Encoding.UTF8.GetBytes(path + "\0");
        unsafe
        {
            fixed (byte* c = componentBytes)
            fixed (byte* p = pathBytes)
            {
                const int stackSize = 256;
                byte* buffer = stackalloc byte[stackSize];
                int length = Engine._api.Component_GetField(this, c, p, buffer, stackSize);
                if (length < 0)
                {
                    return null;
                }
                if (length < stackSize)
                {
                    return Encoding.UTF8.GetString(buffer, length);
                }
                var large = new byte[length + 1];
                fixed (byte* l = large)
                {
                    Engine._api.Component_GetField(this, c, p, l, large.Length);
                }
                return Encoding.UTF8.GetString(large, 0, length);
            }
        }
    }

    public bool SetFieldJson(string component, string path, string json)
    {
        var componentBytes = Encoding.UTF8.GetBytes(component + "\0");
        var pathBytes = Encoding.UTF8.GetBytes(path + "\0");
        var jsonBytes = Encoding.UTF8.GetBytes(json + "\0");
        unsafe
        {
            fixed (byte* c = componentBytes)
            fixed (byte* p = pathBytes)
            fixed (byte* j = jsonBytes)
            {
                return Engine._api.Component_SetField(this, c, p, j) != 0;
            }
        }
    }

    // used by Component's fake-null check, where the type is only known at runtime.
    internal bool HasComponentNamed(string inName)
    {
        var bytes = Encoding.UTF8.GetBytes(inName + "\0");
        unsafe { fixed (byte* p = bytes) return Engine._api.Entity_HasComponent(this, p) != 0; }
    }

    public bool Equals(Entity o) => Index == o.Index && Generation == o.Generation;
    public override bool Equals(object? obj) => obj is Entity e && Equals(e);
    public override int GetHashCode() => HashCode.Combine(Index, Generation);
    public static bool operator ==(Entity a, Entity b) => a.Equals(b);
    public static bool operator !=(Entity a, Entity b) => !a.Equals(b);

    public override string ToString() => $"Entity({Index}:{Generation})";
}

// pre-cache per typename so HasComponent/GetComponent doesn't re-marshal the string on every single call.
internal static class ComponentName<T>
{
    public static readonly byte[] Utf8 = Encoding.UTF8.GetBytes(typeof(T).Name + "\0");
}
