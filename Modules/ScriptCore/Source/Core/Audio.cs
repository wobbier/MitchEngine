namespace ScriptCore;

/// <summary>One-shot sounds from scripts: fire and forget, at a position or not.</summary>
public static unsafe class Audio
{
    /// <summary>
    /// Fire-and-forget on the SFX bus; overlapping plays each get their own voice.
    /// </summary>
    public static void PlayOneShot(string clip, float volume = 1f)
    {
        fixed (byte* p = Engine.Utf8(clip, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Audio_PlayOneShot(p, volume);
    }

    /// <summary>
    /// Positioned in 3D (attenuated by distance from the listener).
    /// </summary>
    public static void PlayOneShotAt(string clip, Vector3 position, float volume = 1f)
    {
        fixed (byte* p = Engine.Utf8(clip, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Audio_PlayOneShotAt(p, &position, volume);
    }
}
