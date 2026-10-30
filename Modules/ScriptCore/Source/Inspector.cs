using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Text.Json;

namespace ScriptCore;

/// <summary>
/// surpress your feelings, hide your fields
/// </summary>
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

    /// <summary>
    /// Of(type) plus every [Save] field the inspector doesn't show (non-public or hidden): what a
    /// save game stores, and what loading one restores.
    /// </summary>
    public static IEnumerable<FieldInfo> WithSaved(Type type)
    {
        foreach (var f in Of(type))
        {
            yield return f;
        }
        for (var t = type; t != null && t != typeof(object); t = t.BaseType)
        {
            foreach (var f in t.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly))
            {
                bool shown = f.IsPublic && !f.IsDefined(typeof(HideInInspectorAttribute), true);
                if (!shown && f.IsDefined(typeof(SaveAttribute), true))
                {
                    yield return f;
                }
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


/// <summary>
/// The script inspector: every public field as a Havana property row, matching the C++
/// reflection inspector (ranges, tooltips, headers, colors, references, lists, nested types).
/// </summary>
public static class Inspector
{
    public static void DrawDefault(object target)
    {
        DrawObjectFields(target);
    }

    // DrawDefault, reporting whether a field was edited (data assets save on it).
    internal static bool DrawObjectFieldsChanged(object target) => DrawObjectFields(target);

    // Draws every visible field of the boxed object, mutating it in place.
    private static bool DrawObjectFields(object target)
    {
        bool changed = false;
        foreach (var f in FieldReflection.Of(target.GetType()))
        {
            changed |= DrawField(target, f);
        }
        return changed;
    }

    private static bool DrawField(object target, FieldInfo f)
    {
        foreach (var space in f.GetCustomAttributes<SpaceAttribute>(true))
        {
            ImGui.Space(space.Height);
        }
        foreach (var header in f.GetCustomAttributes<HeaderAttribute>(true))
        {
            ImGui.Header(header.Text);
        }

        bool readOnly = f.IsDefined(typeof(ReadOnlyAttribute), true);
        if (readOnly)
        {
            ImGui.BeginDisabled();
        }
        object? value = f.GetValue(target);
        bool changed = DrawValue(f.Name, f, f.FieldType, ref value);
        if (changed && !readOnly)
        {
            f.SetValue(target, value);
        }
        if (readOnly)
        {
            ImGui.EndDisabled();
        }

        var tooltip = f.GetCustomAttribute<TooltipAttribute>(true);
        if (tooltip != null)
        {
            ImGui.Tooltip(tooltip.Text);
        }
        return changed && !readOnly;
    }

    // One value row (or subtree). attributes may be null for list elements.
    private static bool DrawValue(string label, FieldInfo? attributes, Type t, ref object? value)
    {
        var range = attributes?.GetCustomAttribute<RangeAttribute>(true);

        if (t == typeof(float))
        {
            float v = (float)(value ?? 0f);
            bool changed = range != null ? ImGui.SliderFloat(label, ref v, range.Min, range.Max) : ImGui.Float(label, ref v);
            if (changed) value = v;
            return changed;
        }
        if (t == typeof(double))
        {
            double v = (double)(value ?? 0.0);
            if (ImGui.Double(label, ref v)) { value = v; return true; }
            return false;
        }
        if (t == typeof(int))
        {
            int v = (int)(value ?? 0);
            bool changed = range != null ? ImGui.SliderInt(label, ref v, (int)range.Min, (int)range.Max) : ImGui.Int(label, ref v);
            if (changed) value = v;
            return changed;
        }
        if (t == typeof(bool))
        {
            bool v = (bool)(value ?? false);
            if (ImGui.Bool(label, ref v)) { value = v; return true; }
            return false;
        }
        if (t == typeof(string))
        {
            string v = (string?)value ?? "";
            var multiline = attributes?.GetCustomAttribute<MultilineAttribute>(true);
            bool changed = multiline != null ? ImGui.InputMultiline(label, ref v, multiline.Lines) : ImGui.InputText(label, ref v);
            if (changed) value = v;
            return changed;
        }
        if (t.IsEnum)
        {
            var values = Enum.GetValues(t);
            var names = Enum.GetNames(t);
            int index = Array.IndexOf(values, value);
            if (index < 0) index = 0;
            if (ImGui.EnumCombo(label, ref index, names))
            {
                value = values.GetValue(index);
                return true;
            }
            return false;
        }
        if (t == typeof(Vector2))
        {
            Vector2 v = (Vector2)(value ?? default(Vector2));
            if (ImGui.Vec2(label, ref v)) { value = v; return true; }
            return false;
        }
        if (t == typeof(Vector3))
        {
            Vector3 v = (Vector3)(value ?? default(Vector3));
            if (ImGui.Vec3(label, ref v)) { value = v; return true; }
            return false;
        }
        if (t == typeof(Vector4))
        {
            Vector4 v = (Vector4)(value ?? default(Vector4));
            if (ImGui.Vec4(label, ref v)) { value = v; return true; }
            return false;
        }
        if (t == typeof(Quaternion))
        {
            Quaternion v = value is Quaternion q ? q : Quaternion.Identity;
            if (ImGui.Quat(label, ref v)) { v.Normalize(); value = v; return true; }
            return false;
        }
        if (t == typeof(Color))
        {
            Color v = value is Color c ? c : Color.White;
            if (ImGui.ColorRow(label, ref v)) { value = v; return true; }
            return false;
        }
        if (t == typeof(EntityRef))
        {
            EntityRef v = (EntityRef)(value ?? default(EntityRef));
            string guid = v.Guid ?? "";
            if (ImGui.EntityRow(label, ref guid))
            {
                v.Guid = guid;
                value = v;
                return true;
            }
            return false;
        }
        if (t == typeof(AssetRef))
        {
            AssetRef v = (AssetRef)(value ?? default(AssetRef));
            string path = v.Path ?? "";
            string filter = attributes?.GetCustomAttribute<AssetFilterAttribute>(true)?.Filter ?? "";
            if (ImGui.AssetRow(label, ref path, filter))
            {
                v.Path = path;
                value = v;
                return true;
            }
            return false;
        }
        if (t.IsArray || (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(List<>)))
        {
            return DrawListValue(label, t, ref value);
        }
        if ((t.IsValueType || t.IsClass) && t.IsDefined(typeof(SerializableAttribute), false))
        {
            return DrawNestedValue(label, t, ref value);
        }

        return false;
    }

    private static bool DrawListValue(string label, Type t, ref object? value)
    {
        Type elementType = t.IsArray ? t.GetElementType()! : t.GetGenericArguments()[0];
        var items = new List<object?>();
        if (value is IEnumerable existing)
        {
            foreach (var item in existing) items.Add(item);
        }

        bool changed = false;
        if (ImGui.TreeNode($"{label} ({items.Count})"))
        {
            for (int i = 0; i < items.Count; ++i)
            {
                object? element = items[i];
                if (DrawValue($"[{i}]", null, elementType, ref element))
                {
                    items[i] = element;
                    changed = true;
                }
            }
            if (ImGui.SmallButton("+"))
            {
                items.Add(elementType.IsValueType ? Activator.CreateInstance(elementType) : elementType == typeof(string) ? "" : null);
                changed = true;
            }
            ImGui.SameLine();
            if (ImGui.SmallButton("-") && items.Count > 0)
            {
                items.RemoveAt(items.Count - 1);
                changed = true;
            }
            ImGui.TreePop();
        }

        if (changed)
        {
            if (t.IsArray)
            {
                var array = Array.CreateInstance(elementType, items.Count);
                for (int i = 0; i < items.Count; ++i) array.SetValue(items[i], i);
                value = array;
            }
            else
            {
                var list = (IList)Activator.CreateInstance(t)!;
                foreach (var item in items) list.Add(item);
                value = list;
            }
        }
        return changed;
    }

    private static bool DrawNestedValue(string label, Type t, ref object? value)
    {
        if (value == null)
        {
            if (!ImGui.TreeNode($"{label} (null)"))
            {
                return false;
            }
            bool created = false;
            if (ImGui.SmallButton("+") && t.GetConstructor(Type.EmptyTypes) != null)
            {
                value = Activator.CreateInstance(t);
                created = true;
            }
            ImGui.TreePop();
            return created;
        }

        bool changed = false;
        if (ImGui.TreeNode(label))
        {
            changed = DrawObjectFields(value);   // mutates the box in place
            ImGui.TreePop();
        }
        return changed;
    }
}


/// <summary>
/// simple JSON serialization for script state, using the same field reflection as the inspector. not super efficient but hey, it's only for editor persistence and maybe some debug dumping, so who cares.
/// </summary>
public static class ScriptSerializer
{
    // Replaced on hot reload: its metadata cache would otherwise pin the unloaded game assembly.
    private static JsonSerializerOptions Options = new() { IncludeFields = true };

    internal static void ResetCaches() => Options = new() { IncludeFields = true };

    public static string Serialize(object target, bool includeSaveFields = false)
    {
        var dict = new Dictionary<string, object?>();
        var fields = includeSaveFields ? FieldReflection.WithSaved(target.GetType()) : FieldReflection.Of(target.GetType());
        foreach (var f in fields)
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
        // Scenes only hold the shown fields; a save game's snapshot also has the [Save] ones.
        foreach (var f in FieldReflection.WithSaved(target.GetType()))
        {
            if (root.TryGetProperty(f.Name, out var el) || TryFormerName(root, f, out el))
            {
                try { f.SetValue(target, el.Deserialize(f.FieldType, Options)); }
                catch (JsonException) { }   // a field that changed type keeps its default
            }
        }
    }

    // A renamed field ([FormerName]) still reads the value saved under its old name.
    private static bool TryFormerName(JsonElement root, FieldInfo field, out JsonElement value)
    {
        foreach (var former in field.GetCustomAttributes<FormerNameAttribute>(false))
        {
            if (root.TryGetProperty(former.Name, out value)) return true;
        }
        value = default;
        return false;
    }
}
