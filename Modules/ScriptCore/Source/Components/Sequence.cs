namespace ScriptCore;

// Authored property tracks on any number of scene objects. Times are in seconds.
public class Sequence : Component
{
    public void Play(bool restart = true) { unsafe { Engine._api.Sequence_Play(Entity, restart ? (byte)1 : (byte)0); } }
    // Names are configured in the Sequencer's Clips tab. False leaves the current playback intact.
    public bool Play(string clipName, bool restart = true)
    {
        if (string.IsNullOrEmpty(clipName)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(clipName)) return Engine._api.Sequence_PlayClip(Entity, p, restart ? (byte)1 : (byte)0) != 0; }
    }
    public bool PlayAsset(string assetPath, bool restart = true)
    {
        if (string.IsNullOrEmpty(assetPath)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(assetPath)) return Engine._api.Sequence_PlayAsset(Entity, p, restart ? (byte)1 : (byte)0) != 0; }
    }
    public bool HasClip(string clipName)
    {
        if (string.IsNullOrEmpty(clipName)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(clipName)) return Engine._api.Sequence_HasClip(Entity, p) != 0; }
    }
    public void Bind(string slot, Entity target)
    {
        if (string.IsNullOrEmpty(slot)) return;
        unsafe { fixed (byte* p = Engine.Utf8(slot)) Engine._api.Sequence_Bind(Entity, p, target); }
    }
    public void Pause() { unsafe { Engine._api.Sequence_Pause(Entity); } }
    public void Stop() { unsafe { Engine._api.Sequence_Stop(Entity); } }
    public void Seek(float seconds) { unsafe { Engine._api.Sequence_Seek(Entity, seconds); } }
    public bool IsPlaying { get { unsafe { return Engine._api.Sequence_IsPlaying(Entity) != 0; } } }
    public float Time { get { unsafe { return Engine._api.Sequence_GetTime(Entity); } } }
    public float Duration { get { unsafe { return Engine._api.Sequence_GetDuration(Entity); } } }
}
