namespace ScriptCore;

public static unsafe class Time
{
    // Scaled game time this frame (0 while paused).
    public static float DeltaTime => Engine._api.Time_GetDeltaTime();
    // Real seconds this frame, ignoring pause and time scale.
    public static float UnscaledDeltaTime => Engine._api.Time_GetUnscaledDeltaTime();
    // The fixed simulation step (OnFixedUpdate).
    public static float FixedDeltaTime => Engine._api.Time_GetFixedDeltaTime();
    // Seconds since scripting started (wall clock).
    public static float RealTime => Engine._api.GetTime();

    public static float TimeScale
    {
        get => Engine._api.Time_GetTimeScale();
        set => Engine._api.Time_SetTimeScale(value);
    }
}
