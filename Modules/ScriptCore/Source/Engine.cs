using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace ScriptCore;

// EngineAPIBindings (the engine's function table) is generated: Generated/EngineAPIBindings.g.cs.

public static unsafe class Engine
{
    internal static EngineAPIBindings _api;

    internal static void Bind(EngineAPIBindings api) => _api = api;

    public static void Log(string msg,
        [CallerMemberName] string member = "",
        [CallerFilePath]   string file   = "",
        [CallerLineNumber] int    line   = 0)
    {
        var full = $"{msg}  [{System.IO.Path.GetFileName(file)}:{line} {member}]";
        int maxBytes = Encoding.UTF8.GetMaxByteCount(full.Length) + 1;
        Span<byte> buf = maxBytes <= 256 ? stackalloc byte[maxBytes] : new byte[maxBytes];
        int written = Encoding.UTF8.GetBytes(full, buf);
        buf[written] = 0;
        fixed (byte* p = buf) _api.Log(p);
    }

    public static float GetTime() => _api.GetTime();

    internal static byte[] Utf8(string s)
    {
        var b = new byte[Encoding.UTF8.GetByteCount(s) + 1];
        Encoding.UTF8.GetBytes(s, b);
        return b;
    }
}

public static class ScriptBridge
{
    private static GameScriptALC? _alc;
    // scaffolding for hot-reload: a weak ref to the collectible ALC so we can later confirm the old
    // one actually unloaded after a reload. not wired up yet (reload path is still commented out).
    private static WeakReference? _alcRef;
    private static readonly Dictionary<int, IGameScript> Instances = new();
    private static int _nextHandle = 1;

    private static readonly List<int> _handleOrder = new();


    [UnmanagedCallersOnly]
    public static unsafe int SetEngineAPI(EngineAPIBindings* api, int size)
    {
        if (size != sizeof(EngineAPIBindings))
        {
            Console.WriteLine($"[ScriptBridge] SetEngineAPI: size mismatch (got {size}, expected {sizeof(EngineAPIBindings)})");
            return -1;
        }
        try
        {
            Engine.Bind(*api);
            Console.WriteLine("[ScriptBridge] Engine API bound.");
            Engine.Log("Engine Time: " + Engine.GetTime().ToString());
            return 0;
        }
        catch (Exception ex)
        {
            ReportException("SetEngineAPI", ex);
            return -1;
        }
    }

    internal static unsafe string ReadUtf8(byte* ptr)
        => ptr is null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)ptr) ?? string.Empty;

    internal static unsafe void WriteUtf8(string s, byte* buf, int size)
    {
        if (buf is null || size <= 0)
        {
            return;
        }

        var bytes = Encoding.UTF8.GetBytes(s);
        int len = Math.Min(bytes.Length, size - 1);
        bytes.AsSpan(0, len).CopyTo(new Span<byte>(buf, size));
        buf[len] = 0;
    }

    // Nothing managed is allowed to throw back across the native boundary - CoreCLR turns an
    // escaped exception into a process kill. Every [UnmanagedCallersOnly] that touches user
    // script code or reflection funnels its failures through here instead.
    private static void ReportException(string where, Exception ex)
        => Console.WriteLine($"[ScriptBridge] {where} threw {ex.GetType().Name}: {ex.Message}");

    // Loads from memory, not the file: the file stays unlocked so a rebuild can replace it while
    // the game runs (hot reload).
    private static (GameScriptALC alc, System.Reflection.Assembly asm) LoadIntoNewContext(string path)
    {
        var alc = new GameScriptALC(path);
        using var image = new System.IO.MemoryStream(System.IO.File.ReadAllBytes(path));
        var pdbPath = System.IO.Path.ChangeExtension(path, ".pdb");
        using var symbols = System.IO.File.Exists(pdbPath) ? new System.IO.MemoryStream(System.IO.File.ReadAllBytes(pdbPath)) : null;
        var asm = alc.LoadFromStream(image, symbols);
        return (alc, asm);
    }

    [UnmanagedCallersOnly]
    public static unsafe int LoadGameAssembly(byte* pathPtr)
    {
        var path = System.IO.Path.GetFullPath(ReadUtf8(pathPtr));
        try
        {
            var (alc, asm) = LoadIntoNewContext(path);
            _alc = alc;
            _alcRef = new WeakReference(_alc);
            ScriptRegistry.Scan(asm);
            Console.WriteLine($"[ScriptBridge] Loaded {ScriptRegistry.ScriptTypes.Count} type(s) from {System.IO.Path.GetFileName(path)}");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[ScriptBridge] LoadGameAssembly failed: {ex.Message}");
            return -1;
        }
    }

    // Hot reload: loads the rebuilt assembly next to the old one, then moves every live script
    // over (same handle, same entity, public fields carried as JSON) and unloads the old context.
    // If the new assembly fails to load, nothing changes. Returns the number of scripts carried
    // over, or -1. Scripts whose type disappeared are dropped (their handles stop responding).
    [UnmanagedCallersOnly]
    public static unsafe int ReloadGameAssembly(byte* pathPtr)
    {
        var path = System.IO.Path.GetFullPath(ReadUtf8(pathPtr));
        GameScriptALC newAlc;
        System.Reflection.Assembly newAsm;
        try
        {
            (newAlc, newAsm) = LoadIntoNewContext(path);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[ScriptBridge] ReloadGameAssembly: the new assembly failed to load, keeping the old one: {ex.Message}");
            return -1;
        }

        var snapshots = new List<(int Handle, string TypeName, Entity Entity, string Fields)>();
        foreach (var handle in _handleOrder)
        {
            if (!Instances.TryGetValue(handle, out var old)) continue;
            string fields = "";
            try { fields = ScriptSerializer.Serialize(old); }
            catch (Exception ex) { ReportException($"Reload snapshot(handle={handle})", ex); }
            snapshots.Add((handle, old.GetType().Name, old is Script s ? s.Entity : Entity.Null, fields));
        }

        Instances.Clear();
        _handleOrder.Clear();
        ScriptRegistry.Clear();
        ScriptSerializer.ResetCaches();
        _alc?.Unload();
        _alc = newAlc;
        _alcRef = new WeakReference(newAlc);
        ScriptRegistry.Scan(newAsm);

        int restored = 0;
        foreach (var (handle, typeName, entity, fields) in snapshots)
        {
            var type = ScriptRegistry.Find(typeName);
            if (type is null)
            {
                Console.WriteLine($"[ScriptBridge] Reload: '{typeName}' no longer exists; dropping handle {handle}");
                continue;
            }
            try
            {
                if (Activator.CreateInstance(type) is not IGameScript instance) continue;
                if (instance is Script script) script.Entity = entity;
                ScriptSerializer.Deserialize(instance, fields);
                Instances[handle] = instance;
                _handleOrder.Add(handle);
                instance.OnReload();
                ++restored;
            }
            catch (Exception ex)
            {
                ReportException($"Reload('{typeName}', handle={handle})", ex);
            }
        }
        GC.Collect();
        GC.WaitForPendingFinalizers();
        Console.WriteLine($"[ScriptBridge] Reloaded {System.IO.Path.GetFileName(path)}: {ScriptRegistry.ScriptTypes.Count} type(s), {restored}/{snapshots.Count} script(s) carried over");
        return restored;
    }

    // 1 while a handle has a live script (a reload drops scripts whose type was deleted).
    [UnmanagedCallersOnly]
    public static int IsHandleAlive(int handle) => Instances.ContainsKey(handle) ? 1 : 0;

    [UnmanagedCallersOnly]
    public static int GetScriptCount() => ScriptRegistry.ScriptTypes.Count;

    // The class of a live script (differs from the name it was created with after a rename).
    [UnmanagedCallersOnly]
    public static unsafe void GetHandleTypeName(int handle, byte* outBuf, int bufSize)
    {
        if (outBuf is not null && bufSize > 0) outBuf[0] = 0;
        if (Instances.TryGetValue(handle, out var s))
        {
            WriteUtf8(s.GetType().Name, outBuf, bufSize);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void GetScriptName(int index, byte* outBuf, int bufSize)
    {
        var types = ScriptRegistry.ScriptTypes;
        if ((uint)index < (uint)types.Count)
        {
            WriteUtf8(types[index].Name, outBuf, bufSize);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int CreateScript(byte* inTypeName, Entity inEntity)
    {
        var name = ReadUtf8(inTypeName);
        try
        {
            // matched by short name (or a [FormerName]) - see the collision note on ScriptRegistry.ScriptTypes
            var type = ScriptRegistry.Find(name);
            if (type is null)
            {
                Console.WriteLine($"[ScriptBridge] CreateScript: no script type named '{name}'");
                return -1;
            }

            // ctor can throw, or the type may have no usable parameterless ctor - both land here.
            if (Activator.CreateInstance(type) is not IGameScript instance)
            {
                Console.WriteLine($"[ScriptBridge] CreateScript: '{name}' is not an IGameScript");
                return -1;
            }

            // new .NET world: the entity is just its id. C# owns the wrapper, C++ only ever sees
            // the EntityID. Scripts that don't derive from Script (raw IGameScript) just don't get one.
            if (instance is Script script)
            {
                script.Entity = inEntity;
            }

            int handle = _nextHandle++;
            Instances[handle] = instance;
            _handleOrder.Add(handle);
            return handle;
        }
        catch (Exception ex)
        {
            ReportException($"CreateScript('{name}')", ex);
            return -1;
        }
    }

    [UnmanagedCallersOnly]
    public static void ScriptOnStart(int handle)
    {
        if (!Instances.TryGetValue(handle, out var s))
        {
            return;
        }

        try
        {
            s.OnStart();
        }
        catch (Exception ex)
        {
            ReportException($"OnStart(handle={handle})", ex);
        }
    }

    [UnmanagedCallersOnly]
    public static void ScriptOnUpdate(int handle, float dt)
    {
        if (!Instances.TryGetValue(handle, out var s))
        {
            return;
        }

        try
        {
            s.OnUpdate(dt);
        }
        catch (Exception ex)
        {
            ReportException($"OnUpdate(handle={handle})", ex);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void ScriptOnCollision(int handle, int kind, Entity other, Vector3* point, Vector3* normal)
    {
        if (!Instances.TryGetValue(handle, out var s))
        {
            return;
        }

        try
        {
            var collision = new Collision { Other = other, Point = *point, Normal = *normal };
            switch (kind)
            {
                case 0: s.OnCollisionEnter(collision); break;
                case 1: s.OnCollisionExit(collision); break;
                case 2: s.OnTriggerEnter(other); break;
                case 3: s.OnTriggerExit(other); break;
            }
        }
        catch (Exception ex)
        {
            ReportException($"Collision callback {kind}(handle={handle})", ex);
        }
    }

    [UnmanagedCallersOnly]
    public static void ScriptOnFixedUpdate(int handle, float dt)
    {
        if (!Instances.TryGetValue(handle, out var s))
        {
            return;
        }

        try
        {
            s.OnFixedUpdate(dt);
        }
        catch (Exception ex)
        {
            ReportException($"OnFixedUpdate(handle={handle})", ex);
        }
    }

    [UnmanagedCallersOnly]
    public static void ScriptOnDestroy(int handle)
    {
        if (!Instances.TryGetValue(handle, out var s)) return;
        try
        {
            s.OnDestroy();
        }
        catch (Exception ex)
        {
            ReportException($"OnDestroy(handle={handle})", ex);
        }
        finally
        {
            // a throwing OnDestroy still has to release the handle, or it leaks forever.
            Instances.Remove(handle);
            _handleOrder.Remove(handle);
        }
    }

    [UnmanagedCallersOnly]
    public static void ScriptOnEditorInspect(int handle)
    {
        if (!Instances.TryGetValue(handle, out var s)) return;
        try
        {
            Inspector.DrawDefault(s); s.OnEditorInspect();
        }
        catch (Exception ex)
        { 
            ReportException($"OnEditorInspect(handle={handle})", ex);
        }
    }

    // Writes the fields as JSON and returns its byte length; when that doesn't fit in bufSize the
    // caller retries with a bigger buffer.
    [UnmanagedCallersOnly]
    public static unsafe int GetFieldsJson(int handle, byte* outBuf, int bufSize)
    {
        if (outBuf is not null && bufSize > 0) outBuf[0] = 0;
        if (!Instances.TryGetValue(handle, out var s)) return 0;
        try
        {
            var json = ScriptSerializer.Serialize(s);
            int length = Encoding.UTF8.GetByteCount(json);
            if (length < bufSize) WriteUtf8(json, outBuf, bufSize);
            return length;
        }
        catch (Exception ex) { ReportException($"GetFieldsJson(handle={handle})", ex); return 0; }
    }

    [UnmanagedCallersOnly]
    public static unsafe void SetFieldsJson(int handle, byte* json)
    {
        if (!Instances.TryGetValue(handle, out var s)) return;
        try { ScriptSerializer.Deserialize(s, ReadUtf8(json)); }
        catch (Exception ex) { ReportException($"SetFieldsJson(handle={handle})", ex); }
    }

}