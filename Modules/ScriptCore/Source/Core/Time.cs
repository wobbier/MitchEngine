namespace ScriptCore;

/// <summary>The game's clock.</summary>
public static unsafe class Time
{
    /// <summary>
    /// Scaled game time this frame (0 while paused).
    /// </summary>
    public static float DeltaTime => Engine._api.Time_GetDeltaTime();
    /// <summary>
    /// Real seconds this frame, ignoring pause and time scale.
    /// </summary>
    public static float UnscaledDeltaTime => Engine._api.Time_GetUnscaledDeltaTime();
    /// <summary>
    /// The fixed simulation step (OnFixedUpdate).
    /// </summary>
    public static float FixedDeltaTime => Engine._api.Time_GetFixedDeltaTime();
    /// <summary>
    /// Seconds since scripting started (wall clock).
    /// </summary>
    public static float RealTime => Engine._api.GetTime();

    /// <summary>How fast game time runs (1 = normal, 0 = stopped).</summary>
    public static float TimeScale
    {
        get => Engine._api.Time_GetTimeScale();
        set => Engine._api.Time_SetTimeScale(value);
    }
}
