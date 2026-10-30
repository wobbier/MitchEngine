namespace ScriptCore;

/// <summary>
/// The entity's AudioSource (CS-5 b): play, stop, pause; fields (Volume, Pitch, Loop, Bus...)
/// through GetField / SetField like any component.
/// </summary>
public class AudioSource : Component
{
    /// <summary>Plays the clip (looping if the source's Loop is set).</summary>
    public void Play() { unsafe { Engine._api.AudioSource_Play(Entity); } }
    /// <summary>Stops the clip.</summary>
    public void Stop() { unsafe { Engine._api.AudioSource_Stop(Entity); } }
    /// <summary>Pauses the clip where it is.</summary>
    public void Pause() { unsafe { Engine._api.AudioSource_Pause(Entity); } }
    /// <summary>Whether the clip is playing (paused counts; PlaybackSeconds is -1 while paused).</summary>
    public bool IsPlaying { get { unsafe { return Engine._api.AudioSource_IsPlaying(Entity) != 0; } } }
    /// <summary>
    /// NEXT-8: the playing clip's position as the audio mixer sees it, in seconds (sample-accurate,
    /// what Kit.BeatClock follows); -1 while stopped or paused, -2 without an audio engine.
    /// </summary>
    public double PlaybackSeconds { get { unsafe { return Engine._api.AudioSource_GetPlaybackSeconds != null ? Engine._api.AudioSource_GetPlaybackSeconds(Entity) : -2.0; } } }
}
