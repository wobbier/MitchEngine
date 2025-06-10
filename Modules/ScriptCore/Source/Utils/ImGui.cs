using System.Runtime.CompilerServices;

public class ImGui
{
    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static bool ImGui_Begin(string Name);
    public static bool Begin(string Name)
    {
        return ImGui_Begin(Name);
    }


    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void ImGui_End();
    public static void End()
    {
        ImGui_End();
    }


    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void ImGui_Text(string inString);
    public static void Text(string inString)
    {
        ImGui_Text(inString);
    }


    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void ImGui_Checkbox(string inString, ref bool inValue);
    public static void Checkbox(string inString, ref bool inValue)
    {
        ImGui_Checkbox(inString, ref inValue);
    }


    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static bool ImGui_Button(string inString);
    public static bool Button(string inString)
    {
        return ImGui_Button(inString);
    }
}
