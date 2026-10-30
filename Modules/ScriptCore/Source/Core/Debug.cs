namespace ScriptCore;

/// <summary>Logging and debug drawing.</summary>
public static unsafe class Debug
{
    /// <summary>Logs a message (with the calling file and line).</summary>
    public static void Log(string message) => Engine.Log(message);
    /// <summary>Logs a value's ToString().</summary>
    public static void Log(object value) => Engine.Log(value?.ToString() ?? "null");

    /// <summary>Logs a warning.</summary>
    public static void Warning(string message)
    {
        fixed (byte* p = Engine.Utf8(message, stackalloc byte[Engine.Utf8StackSize])) Engine._api.LogWarning(p);
    }

    /// <summary>Logs an error.</summary>
    public static void Error(string message)
    {
        fixed (byte* p = Engine.Utf8(message, stackalloc byte[Engine.Utf8StackSize])) Engine._api.LogError(p);
    }

    /// <summary>Logs a value's ToString() as an error.</summary>
    public static void Error(object value) => Error(value?.ToString() ?? "null");

    /// <summary>
    /// Lines and spheres in the game and scene views; duration 0 draws for this frame only.
    /// </summary>
    public static void DrawLine(Vector3 from, Vector3 to, Vector3 color, float duration = 0f)
        => Engine._api.Debug_DrawLine(&from, &to, &color, duration);

    /// <summary>Draws a wire sphere (see DrawLine).</summary>
    public static void DrawSphere(Vector3 center, float radius, Vector3 color, float duration = 0f)
        => Engine._api.Debug_DrawSphere(&center, radius, &color, duration);
}
