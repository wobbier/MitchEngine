namespace ScriptCore;

// TODO: errything an error for now
public static class Debug
{
    public static void Log(string inString) => Engine.Log(inString);
    public static void Log(object inObject) => Engine.Log(inObject?.ToString() ?? "null");
    public static void Error(string inString) => Engine.Log(inString);
    public static void Error(object inObject) => Engine.Log(inObject?.ToString() ?? "null");
}
