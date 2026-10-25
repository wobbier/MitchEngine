using System.Text;

namespace ScriptCore;

// Immediate-mode ImGui for game scripts (editor inspector widgets live on EngineAPI separately).
public static unsafe class ImGui
{
    // Rows return true on edit and lay out like every C++ inspector (label column, then value).

    public static bool Float(string label, ref float value)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (float* v = &value) return Engine._api.ImGui_Float(p, v) != 0;
    }

    public static bool Double(string label, ref double value)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (double* v = &value) return Engine._api.ImGui_Double(p, v) != 0;
    }

    public static bool Int(string label, ref int value)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (int* v = &value) return Engine._api.ImGui_Int(p, v) != 0;
    }

    public static bool Bool(string label, ref bool value)
    {
        byte v = value ? (byte)1 : (byte)0;
        bool changed;
        fixed (byte* p = Engine.Utf8(label)) changed = Engine._api.ImGui_Bool(p, &v) != 0;
        value = v != 0;
        return changed;
    }

    public static bool SliderFloat(string label, ref float value, float min, float max)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (float* v = &value) return Engine._api.ImGui_SliderFloat(p, v, min, max) != 0;
    }

    public static bool SliderInt(string label, ref int value, int min, int max)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (int* v = &value) return Engine._api.ImGui_SliderInt(p, v, min, max) != 0;
    }

    public static bool InputText(string label, ref string value, int capacity = 512)
    {
        var buffer = TextBuffer(value, capacity);
        bool changed;
        fixed (byte* p = Engine.Utf8(label)) fixed (byte* b = buffer) changed = Engine._api.ImGui_String(p, b, buffer.Length) != 0;
        if (changed) value = FromTextBuffer(buffer);
        return changed;
    }

    public static bool InputMultiline(string label, ref string value, int lines = 4, int capacity = 4096)
    {
        var buffer = TextBuffer(value, capacity);
        bool changed;
        fixed (byte* p = Engine.Utf8(label)) fixed (byte* b = buffer) changed = Engine._api.ImGui_Multiline(p, b, buffer.Length, lines) != 0;
        if (changed) value = FromTextBuffer(buffer);
        return changed;
    }

    public static bool Vec2(string label, ref Vector2 value)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (Vector2* v = &value) return Engine._api.ImGui_Vec2(p, v) != 0;
    }

    public static bool Vec3(string label, ref Vector3 value)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (Vector3* v = &value) return Engine._api.ImGui_Vec3(p, v) != 0;
    }

    public static bool Vec4(string label, ref Vector4 value)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (Vector4* v = &value) return Engine._api.ImGui_Vec4(p, (float*)v) != 0;
    }

    public static bool Quat(string label, ref Quaternion value)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (Quaternion* v = &value) return Engine._api.ImGui_Vec4(p, (float*)v) != 0;
    }

    public static bool ColorRow(string label, ref Color value)
    {
        fixed (byte* p = Engine.Utf8(label)) fixed (Color* v = &value) return Engine._api.ImGui_Color(p, (float*)v) != 0;
    }

    public static bool EnumCombo(string label, ref int index, string[] names)
    {
        bool changed;
        fixed (byte* p = Engine.Utf8(label)) fixed (int* i = &index) fixed (byte* n = Engine.Utf8(string.Join('\n', names)))
            changed = Engine._api.ImGui_Enum(p, i, n) != 0;
        return changed;
    }

    public static void Header(string text)
    {
        fixed (byte* p = Engine.Utf8(text)) Engine._api.ImGui_Header(p);
    }

    public static void Space(float height = 0f) => Engine._api.ImGui_Space(height);

    // Hover text for the previous row.
    public static void Tooltip(string text)
    {
        fixed (byte* p = Engine.Utf8(text)) Engine._api.ImGui_Tooltip(p);
    }

    public static void BeginDisabled() => Engine._api.ImGui_BeginDisabled();
    public static void EndDisabled() => Engine._api.ImGui_EndDisabled();

    public static bool TreeNode(string label)
    {
        fixed (byte* p = Engine.Utf8(label)) return Engine._api.ImGui_TreeNode(p) != 0;
    }

    public static void TreePop() => Engine._api.ImGui_TreePop();

    public static bool SmallButton(string label)
    {
        fixed (byte* p = Engine.Utf8(label)) return Engine._api.ImGui_SmallButton(p) != 0;
    }

    public static void SameLine() => Engine._api.ImGui_SameLine();

    // An entity row on the 16-hex scene GUID an EntityRef stores.
    public static bool EntityRow(string label, ref string guid)
    {
        var buffer = TextBuffer(guid, 64);
        bool changed;
        fixed (byte* p = Engine.Utf8(label)) fixed (byte* b = buffer) changed = Engine._api.ImGui_EntityRef(p, b, buffer.Length) != 0;
        if (changed) guid = FromTextBuffer(buffer);
        return changed;
    }

    // An asset row on a project path; filter narrows the picker ("Texture", or an extension).
    public static bool AssetRow(string label, ref string path, string filter = "")
    {
        var buffer = TextBuffer(path, 512);
        bool changed;
        fixed (byte* p = Engine.Utf8(label)) fixed (byte* b = buffer) fixed (byte* f = Engine.Utf8(filter ?? ""))
            changed = Engine._api.ImGui_AssetRef(p, b, buffer.Length, f) != 0;
        if (changed) path = FromTextBuffer(buffer);
        return changed;
    }

    private static byte[] TextBuffer(string? value, int capacity)
    {
        value ??= "";
        var buffer = new byte[System.Math.Max(capacity, Encoding.UTF8.GetByteCount(value) + 1)];
        Encoding.UTF8.GetBytes(value, buffer);
        return buffer;
    }

    private static string FromTextBuffer(byte[] buffer)
    {
        int length = System.Array.IndexOf(buffer, (byte)0);
        return Encoding.UTF8.GetString(buffer, 0, length < 0 ? buffer.Length : length);
    }
    public static bool Begin(string name)
    {
        fixed (byte* p = Engine.Utf8(name)) return Engine._api.ImGui_Begin(p) != 0;
    }

    public static void Text(string text)
    {
        fixed (byte* p = Engine.Utf8(text)) Engine._api.ImGui_Text(p);
    }

    public static bool Checkbox(string label, ref bool value)
    {
        byte v = value ? (byte)1 : (byte)0;
        bool changed;
        fixed (byte* p = Engine.Utf8(label)) changed = Engine._api.ImGui_Checkbox(p, &v) != 0;
        value = v != 0;
        return changed;
    }

    public static bool Button(string label)
    {
        fixed (byte* p = Engine.Utf8(label)) return Engine._api.ImGui_Button(p) != 0;
    }

    public static void End() => Engine._api.ImGui_End();
}
