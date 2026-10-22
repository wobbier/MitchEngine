namespace ScriptCore;

// Immediate-mode ImGui for game scripts (editor inspector widgets live on EngineAPI separately).
public static unsafe class ImGui
{
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
