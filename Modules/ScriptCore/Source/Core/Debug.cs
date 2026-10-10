namespace ScriptCore;

public static unsafe class Debug
{
    public static void Log(string message) => Engine.Log(message);
    public static void Log(object value) => Engine.Log(value?.ToString() ?? "null");

    public static void Warning(string message)
    {
        fixed (byte* p = Engine.Utf8(message)) Engine._api.LogWarning(p);
    }

    public static void Error(string message)
    {
        fixed (byte* p = Engine.Utf8(message)) Engine._api.LogError(p);
    }

    public static void Error(object value) => Error(value?.ToString() ?? "null");

    // Lines and spheres in the game and scene views; duration 0 draws for this frame only.
    public static void DrawLine(Vector3 from, Vector3 to, Vector3 color, float duration = 0f)
        => Engine._api.Debug_DrawLine(&from, &to, &color, duration);

    public static void DrawSphere(Vector3 center, float radius, Vector3 color, float duration = 0f)
        => Engine._api.Debug_DrawSphere(&center, radius, &color, duration);
}
