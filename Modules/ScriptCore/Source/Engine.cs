using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace ScriptCore;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct EngineAPIBindings
{
    public delegate* unmanaged<byte*, void> Log;
    public delegate* unmanaged<float> GetTime;

    // Entity and components
    public delegate* unmanaged<Entity, byte> Entity_IsAlive;
    public delegate* unmanaged<Entity, byte*, byte> Entity_HasComponent;
    public delegate* unmanaged<Entity, byte*, void> Entity_AddComponent;
    public delegate* unmanaged<Entity, Vector3*, void> Transform_GetTranslation;
    public delegate* unmanaged<Entity, Vector3*, void> Transform_SetTranslation;

    // ImGui
    public delegate* unmanaged<byte*, float*, byte> ImGui_Float;
    public delegate* unmanaged<byte*, int*, byte> ImGui_Int;
    public delegate* unmanaged<byte*, byte*, byte> ImGui_Bool;
    public delegate* unmanaged<byte*, Vector3*, byte> ImGui_Vec3;

    public delegate* unmanaged<byte*, byte> ImGui_Begin;
    public delegate* unmanaged<byte*, void> ImGui_Text;
    public delegate* unmanaged<byte*, byte*, byte> ImGui_Checkbox;
    public delegate* unmanaged<byte*, byte> ImGui_Button;
    public delegate* unmanaged<void> ImGui_End;

    // Engine shit
    public delegate* unmanaged<int, byte> Input_IsKeyDown;

    public delegate* unmanaged<byte*, Entity*, void> World_CreateEntity;
    public delegate* unmanaged<byte*, Entity*, void> World_FindByName;

    public delegate* unmanaged<Entity, Vector3*, void> Camera_GetClearColor;
    public delegate* unmanaged<Entity, Vector3*, void> Camera_SetClearColor;

    public delegate* unmanaged<Entity, Vector3*, void> Transform_GetScale;
    public delegate* unmanaged<Entity, Vector3*, void> Transform_SetScale;
    public delegate* unmanaged<Entity, Vector3*, void> Transform_GetRotation;
    public delegate* unmanaged<Entity, Vector3*, void> Transform_SetRotation;

    public delegate* unmanaged<Entity, byte*, void> BasicUIView_ExecuteJS;
}

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

    private record FieldSnapshot(string TypeName, Dictionary<string, string> Fields);
    private static readonly Dictionary<int, FieldSnapshot> _snapshots = new();

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

    [UnmanagedCallersOnly]
    public static unsafe int LoadGameAssembly(byte* pathPtr)
    {
        var path = System.IO.Path.GetFullPath(ReadUtf8(pathPtr));
        try
        {
            _alc = new GameScriptALC(path);
            _alcRef = new WeakReference(_alc);
            var asm = _alc.LoadFromAssemblyPath(path);
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

    [UnmanagedCallersOnly]
    public static int GetScriptCount() => ScriptRegistry.ScriptTypes.Count;

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
            // matched by short name - see the collision note on ScriptRegistry.ScriptTypes
            var type = ScriptRegistry.ScriptTypes.FirstOrDefault(t => t.Name == name);
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

    [UnmanagedCallersOnly]
    public static unsafe void GetFieldsJson(int handle, byte* outBuf, int bufSize)
    {
        if (outBuf is null || bufSize <= 0) return;
        outBuf[0] = 0;
        if (!Instances.TryGetValue(handle, out var s)) return;
        try { WriteUtf8(ScriptSerializer.Serialize(s), outBuf, bufSize); }
        catch (Exception ex) { ReportException($"GetFieldsJson(handle={handle})", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe void SetFieldsJson(int handle, byte* json)
    {
        if (!Instances.TryGetValue(handle, out var s)) return;
        try { ScriptSerializer.Deserialize(s, ReadUtf8(json)); }
        catch (Exception ex) { ReportException($"SetFieldsJson(handle={handle})", ex); }
    }

}