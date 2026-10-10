namespace ScriptCore;

public static unsafe class Audio
{
    // Fire-and-forget on the SFX bus; overlapping plays each get their own voice.
    public static void PlayOneShot(string clip, float volume = 1f)
    {
        fixed (byte* p = Engine.Utf8(clip)) Engine._api.Audio_PlayOneShot(p, volume);
    }

    // Positioned in 3D (attenuated by distance from the listener).
    public static void PlayOneShotAt(string clip, Vector3 position, float volume = 1f)
    {
        fixed (byte* p = Engine.Utf8(clip)) Engine._api.Audio_PlayOneShotAt(p, &position, volume);
    }
}
