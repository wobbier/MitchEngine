using System;
using System.Runtime.InteropServices;
using System.Text;

namespace ScriptCore;

/// <summary>
/// struct because it's mapped to the engine EntityID struct. don't fuckin add anything to this &lt;3 or at least update it to reflect it
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct Entity : IEquatable<Entity>
{
    /// <summary>{0, 0}</summary>
    public static readonly Entity Null = default;

    /// <summary>
    /// Mirrors the engine's 8-byte EntityID: slot index + generation (0 = null).
    /// </summary>
    public readonly uint Index;
    /// <summary>The slot's generation: a handle to a destroyed entity stops matching when the slot is reused.</summary>
    public readonly uint Generation;

    /// <summary>A handle from its index and generation (scripts get these from the engine, not by hand).</summary>
    public Entity(uint inIndex, uint inGeneration)
    {
        Index = inIndex;
        Generation = inGeneration;
    }

    /// <summary>Whether the entity still exists.</summary>
    public bool IsAlive { get { unsafe { return Engine._api.Entity_IsAlive(this) != 0; } } }

    /// <summary>
    /// Its direct children (CS-5 h).
    /// </summary>
    public Entity[] Children
    {
        get
        {
            Entity self = this;
            return Query.Fill((buffer, max) => { unsafe { fixed (Entity* b = buffer) return Engine._api.Transform_GetChildren(self, b, max); } });
        }
    }

    /// <summary>
    /// The scene GUID (the 16-hex string scenes and EntityRef store), "" for a dead entity.
    /// </summary>
    public string Guid
    {
        get
        {
            var buffer = new byte[32];
            int length;
            unsafe { fixed (byte* b = buffer) length = Engine._api.Entity_GetGuid(this, b, buffer.Length); }
            return System.Text.Encoding.UTF8.GetString(buffer, 0, System.Math.Clamp(length, 0, buffer.Length - 1));
        }
    }

    /// <summary>
    /// The physics / filtering layer, 0..31 (Project Settings names them).
    /// </summary>
    public int Layer
    {
        get { unsafe { return Engine._api.Entity_GetLayer(this); } }
        set { unsafe { Engine._api.Entity_SetLayer(this, value); } }
    }

    /// <summary>
    /// Destroys the entity and its children at the end of the frame.
    /// </summary>
    public void Destroy()
    {
        unsafe { Engine._api.Entity_Destroy(this); }
    }

    /// <summary>Whether the entity is active (an inactive entity and its children don't update or draw).</summary>
    public bool Active
    {
        get { unsafe { return Engine._api.Entity_IsActive(this) != 0; } }
        set { unsafe { Engine._api.Entity_SetActive(this, value ? (byte)1 : (byte)0); } }
    }

    /// <summary>The entity's name.</summary>
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
        set { unsafe { fixed (byte* p = Engine.Utf8(value, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Entity_SetName(this, p); } }
    }

    /// <summary>
    /// what is life?
    /// </summary>
    public static implicit operator bool(Entity e) => e.IsAlive;

    /// <summary>
    /// Any registered component by name ("Light", "ParticleSystem", "AudioSource"...), wrapped in
    /// C# or not; its fields are reachable through Component.GetField / SetField.
    /// </summary>
    public bool HasComponent(string component)
    {
        unsafe { fixed (byte* p = Engine.Utf8(component, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Entity_HasComponent(this, p) != 0; }
    }

    /// <summary>Adds an engine component by registry name ("Light", "Rigidbody").</summary>
    public void AddComponent(string component)
    {
        unsafe { fixed (byte* p = Engine.Utf8(component, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Entity_AddComponent(this, p); }
    }

    /// <summary>Whether the entity has a component of type T.</summary>
    public bool HasComponent<T>() where T : Component, new()
    {
        unsafe { fixed (byte* p = ComponentName<T>.Utf8) return Engine._api.Entity_HasComponent(this, p) != 0; }
    }

    /// <summary>The entity's component of type T, or null.</summary>
    public T GetComponent<T>() where T : Component, new()
        => HasComponent<T>() ? new T { Entity = this } : null;

    /// <summary>The component of type T and true, or null and false.</summary>
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

    /// <summary>Adds a component of type T and returns it.</summary>
    public T AddComponent<T>() where T : Component, new()
    {
        unsafe { fixed (byte* p = ComponentName<T>.Utf8) Engine._api.Entity_AddComponent(this, p); }
        return GetComponent<T>();
    }

    /// <summary>The entity's Transform.</summary>
    public Transform Transform => GetComponent<Transform>();

    /// <summary>
    /// The entity's script when it is (or implements) T: GetScript&lt;Door&gt;(), GetScript&lt;IHealth&gt;().
    /// Null when it has none. A scan of the live scripts; fine for events, not for every entity
    /// every frame.
    /// </summary>
    public T? GetScript<T>() where T : class => ScriptBridge.FindScript<T>(this);

    /// <summary>Any field of any component by name, including ones without a C# wrapper: GetField&lt;float&gt;("Light", "Intensity"). Paths can go deeper ("Settings.Radius", "Color.0"); a missing component or field returns the fallback.</summary>
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

    /// <summary>Sets a field of any component by name (see GetField); false when the component or field is missing.</summary>
    public bool SetField<T>(string component, string path, T value) => SetFieldJson(component, path, FieldJson.Write(value));

    /// <summary>A component field as JSON text, or null when it's missing.</summary>
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

    /// <summary>Sets a component field from JSON text; false when the component or field is missing.</summary>
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

    /// <summary>Same entity: index and generation match.</summary>
    public bool Equals(Entity o) => Index == o.Index && Generation == o.Generation;
    /// <summary>Same entity.</summary>
    public override bool Equals(object? obj) => obj is Entity e && Equals(e);
    /// <summary>A hash of index and generation.</summary>
    public override int GetHashCode() => HashCode.Combine(Index, Generation);
    /// <summary>Same entity.</summary>
    public static bool operator ==(Entity a, Entity b) => a.Equals(b);
    /// <summary>Different entities.</summary>
    public static bool operator !=(Entity a, Entity b) => !a.Equals(b);

    /// <summary>"Entity(index:generation)".</summary>
    public override string ToString() => $"Entity({Index}:{Generation})";
}

// pre-cache per typename so HasComponent/GetComponent doesn't re-marshal the string on every single call.
internal static class ComponentName<T>
{
    public static readonly byte[] Utf8 = Encoding.UTF8.GetBytes(typeof(T).Name + "\0");
}
