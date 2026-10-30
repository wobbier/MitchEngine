using System;

namespace ScriptCore;

// Authored property tracks on any number of scene objects. Times are in seconds.
public class Sequence : Component
{
    public void Play(bool restart = true) { unsafe { Engine._api.Sequence_Play(Entity, restart ? (byte)1 : (byte)0); } }
    // Names are configured in the Sequencer's Clips tab. False leaves the current playback intact.
    public bool Play(string clipName, bool restart = true)
    {
        if (string.IsNullOrEmpty(clipName)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(clipName, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Sequence_PlayClip(Entity, p, restart ? (byte)1 : (byte)0) != 0; }
    }
    public bool PlayAsset(string assetPath, bool restart = true)
    {
        if (string.IsNullOrEmpty(assetPath)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(assetPath, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Sequence_PlayAsset(Entity, p, restart ? (byte)1 : (byte)0) != 0; }
    }
    public bool HasClip(string clipName)
    {
        if (string.IsNullOrEmpty(clipName)) return false;
        unsafe { fixed (byte* p = Engine.Utf8(clipName, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Sequence_HasClip(Entity, p) != 0; }
    }
    public void Bind(string slot, Entity target)
    {
        if (string.IsNullOrEmpty(slot)) return;
        unsafe { fixed (byte* p = Engine.Utf8(slot, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Sequence_Bind(Entity, p, target); }
    }
    public void Pause() { unsafe { Engine._api.Sequence_Pause(Entity); } }
    public void Stop() { unsafe { Engine._api.Sequence_Stop(Entity); } }
    public void Seek(float seconds) { unsafe { Engine._api.Sequence_Seek(Entity, seconds); } }
    public bool IsPlaying { get { unsafe { return Engine._api.Sequence_IsPlaying(Entity) != 0; } } }
    public float Time { get { unsafe { return Engine._api.Sequence_GetTime(Entity); } } }
    public float Duration { get { unsafe { return Engine._api.Sequence_GetDuration(Entity); } } }

    // This player's markers and notifications (GAME-4). Like Script.On, a script's handler goes
    // when it's destroyed (and on a hot reload); Dispose the result to stop sooner.
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

public enum SequenceEventKind { Marker, Started, Finished, Interrupted, Looped }

// A Sequencer marker crossed during playback (Kind Marker: Name, Payload and Number as authored), or
// a playback notification (Name is the sequence's). Markers go to the marker's receiver object,
// notifications to the player; both are also published on the EventBus. Receiver is Entity.Null
// for a marker whose binding slot isn't bound.
public readonly record struct SequenceEvent(Entity Player, Entity Receiver, SequenceEventKind Kind, string Name, string Payload, float Number, int Frame, string Clip);

// A script on a receiver object gets its sequence events here, before EventBus subscribers.
public interface ISequenceReceiver
{
    void OnSequenceEvent(SequenceEvent e);
}
