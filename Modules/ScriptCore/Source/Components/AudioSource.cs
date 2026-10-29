namespace ScriptCore;

// The entity's AudioSource (CS-5 b): play, stop, pause; fields (Volume, Pitch, Loop, Bus...)
// through GetField / SetField like any component.
public class AudioSource : Component
{
    public void Play() { unsafe { Engine._api.AudioSource_Play(Entity); } }
    public void Stop() { unsafe { Engine._api.AudioSource_Stop(Entity); } }
    public void Pause() { unsafe { Engine._api.AudioSource_Pause(Entity); } }
    public bool IsPlaying { get { unsafe { return Engine._api.AudioSource_IsPlaying(Entity) != 0; } } }
}
