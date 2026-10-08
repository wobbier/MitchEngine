using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Text.Json;

namespace ScriptCore;

// surpress your feelings, hide your fields
[AttributeUsage(AttributeTargets.Field)]
public sealed class HideInInspectorAttribute : Attribute { }

internal static class FieldReflection
{
    public static IEnumerable<FieldInfo> Of(Type type)
    {
        foreach (var f in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
        {
            if (!f.IsDefined(typeof(HideInInspectorAttribute), true))
            {
                yield return f;
            }
        }
    }

    public static byte[] Utf8(string s)
    {
        var b = new byte[Encoding.UTF8.GetByteCount(s) + 1];
        Encoding.UTF8.GetBytes(s, b);
        return b;
    }
}


// imgui inspection for base shit atm, more to come
public static unsafe class Inspector
{
    public static void DrawDefault(object target)
    {
        foreach (var f in FieldReflection.Of(target.GetType()))
        {
            DrawField(target, f);
        }
    }

    private static void DrawField(object target, FieldInfo f)
    {
        var t = f.FieldType;

        if (t == typeof(float))
        {
            float v = (float)f.GetValue(target);
            fixed (byte* l = FieldReflection.Utf8(f.Name))
                if (Engine._api.ImGui_Float(l, &v) != 0) f.SetValue(target, v);
        }
        else if (t == typeof(int))
        {
            int v = (int)f.GetValue(target);
            fixed (byte* l = FieldReflection.Utf8(f.Name))
                if (Engine._api.ImGui_Int(l, &v) != 0) f.SetValue(target, v);
        }
        else if (t == typeof(bool))
        {
            byte v = (bool)f.GetValue(target) ? (byte)1 : (byte)0;
            fixed (byte* l = FieldReflection.Utf8(f.Name))
                if (Engine._api.ImGui_Bool(l, &v) != 0) f.SetValue(target, v != 0);
        }
        else if (t == typeof(Vector3))
        {
            Vector3 v = (Vector3)f.GetValue(target);
            fixed (byte* l = FieldReflection.Utf8(f.Name))
                if (Engine._api.ImGui_Vec3(l, &v) != 0) f.SetValue(target, v);
        }
    }
}


// simple JSON serialization for script state, using the same field reflection as the inspector. not super efficient but hey, it's only for editor persistence and maybe some debug dumping, so who cares.
public static class ScriptSerializer
{
    private static readonly JsonSerializerOptions Options = new() { IncludeFields = true };

    public static string Serialize(object target)
    {
        var dict = new Dictionary<string, object?>();
        foreach (var f in FieldReflection.Of(target.GetType()))
        {
            dict[f.Name] = f.GetValue(target);
        }

        return JsonSerializer.Serialize(dict, Options);
    }

    public static void Deserialize(object target, string json)
    {
        if (string.IsNullOrEmpty(json)) return;
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        foreach (var f in FieldReflection.Of(target.GetType()))
        if (root.TryGetProperty(f.Name, out var el))
        {
            f.SetValue(target, el.Deserialize(f.FieldType, Options));
        }
    }
}
