using System;

namespace ScriptCore;

/// <summary>
/// Authored property tracks on any number of scene objects. Times are in seconds.
/// </summary>
public class Sequence : Component
{
    /// <summary>Plays the sequence's clip (from the start unless restart is false).</summary>
    public void Play(bool restart = true) { unsafe { Engine._api.Sequence_Play(Entity, restart ? (byte)1 : (byte)0); } }
    /// <summary>
    /// Names are configured in the Sequencer's Clips tab. False leaves the current playback intact.
    /// </summary>
    public bool Play(string clipName, bool restart = true)
    {
        if (string.IsNullOrEmpty(clipName)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(clipName, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Sequence_PlayClip(Entity, p, restart ? (byte)1 : (byte)0) != 0; }
    }
    /// <summary>Plays a .sequence asset on this player, binding its slots by name; false when it can't be loaded.</summary>
    public bool PlayAsset(string assetPath, bool restart = true)
    {
        if (string.IsNullOrEmpty(assetPath)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(assetPath, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Sequence_PlayAsset(Entity, p, restart ? (byte)1 : (byte)0) != 0; }
    }
    /// <summary>Whether the player has a clip of that name (the Sequencer's Clips tab).</summary>
    public bool HasClip(string clipName)
    {
        if (string.IsNullOrEmpty(clipName)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(clipName, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Sequence_HasClip(Entity, p) != 0; }
    }
    /// <summary>Binds a clip's slot to an entity (what its tracks animate).</summary>
    public void Bind(string slot, Entity target)
    {
        if (string.IsNullOrEmpty(slot)) return;
        unsafe { fixed (byte* p = Engine.Utf8(slot, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Sequence_Bind(Entity, p, target); }
    }
    /// <summary>Pauses at the current time.</summary>
    public void Pause() { unsafe { Engine._api.Sequence_Pause(Entity); } }
    /// <summary>Stops and rewinds.</summary>
    public void Stop() { unsafe { Engine._api.Sequence_Stop(Entity); } }
    /// <summary>Moves the playhead to a time in seconds.</summary>
    public void Seek(float seconds) { unsafe { Engine._api.Sequence_Seek(Entity, seconds); } }
    /// <summary>Whether it's playing.</summary>
    public bool IsPlaying { get { unsafe { return Engine._api.Sequence_IsPlaying(Entity) != 0; } } }
    /// <summary>The playhead, in seconds.</summary>
    public float Time { get { unsafe { return Engine._api.Sequence_GetTime(Entity); } } }
    /// <summary>The clip's length, in seconds.</summary>
    public float Duration { get { unsafe { return Engine._api.Sequence_GetDuration(Entity); } } }

    /// <summary>
    /// This player's markers and notifications (GAME-4). Like Script.On, a script's handler goes
    /// when it's destroyed (and on a hot reload); Dispose the result to stop sooner.
    /// </summary>
    public IDisposable OnEvent(Action<SequenceEvent> handler) => EventBus.Subscribe<SequenceEvent>(new PlayerFilter(Entity, handler).Invoke);

    private sealed class PlayerFilter : EventBus.IOwnedHandler
    {
        private readonly Entity _player;
        private readonly Action<SequenceEvent> _handler;
        public PlayerFilter(Entity player, Action<SequenceEvent> handler) { _player = player; _handler = handler; }
        public object? Owner => _handler.Target;
        public void Invoke(SequenceEvent e) { if (e.Player == _player) _handler(e); }
    }
}

/// <summary>What a SequenceEvent reports.</summary>
public enum SequenceEventKind
{
    /// <summary>A marker the playhead crossed.</summary>
    Marker,
    /// <summary>Playback started.</summary>
    Started,
    /// <summary>The clip played to its end.</summary>
    Finished,
    /// <summary>Playback stopped, or another clip took over, before the end.</summary>
    Interrupted,
    /// <summary>A looping clip wrapped around.</summary>
    Looped,
}

/// <summary>
/// A Sequencer marker crossed during playback (Kind Marker: Name, Payload and Number as authored), or
/// a playback notification (Name is the sequence's). Markers go to the marker's receiver object,
/// notifications to the player; both are also published on the EventBus. Receiver is Entity.Null
/// for a marker whose binding slot isn't bound.
/// </summary>
public readonly record struct SequenceEvent(Entity Player, Entity Receiver, SequenceEventKind Kind, string Name, string Payload, float Number, int Frame, string Clip);

/// <summary>
/// A script on a receiver object gets its sequence events here, before EventBus subscribers.
/// </summary>
public interface ISequenceReceiver
{
    /// <summary>A marker or notification addressed to this object (before EventBus subscribers).</summary>
    void OnSequenceEvent(SequenceEvent e);
}
