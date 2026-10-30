using System;

namespace ScriptCore.Kit;

/// <summary>
/// Published on the EventBus by a BeatClock with an owner: each beat it crosses, once.
/// </summary>
public readonly record struct BeatEvent(Entity Owner, int Beat, int Bar, int BeatInBar);

// NEXT-8: a music clock for rhythm and beat-synced gameplay. It follows an AudioSource's playback
// position as the audio mixer sees it (sample-accurate, not frame time), smoothed between mixer
// updates, and fires each beat and bar it crosses exactly once, in order, even across hitches.
// A script owns one and ticks it:
//   private readonly BeatClock clock = new(bpm: 120f, beatsPerBar: 4, offsetSeconds: 0.05);
//   public override void OnStart() { clock.OnBeat += (beat, bar, inBar) => Pulse(inBar == 0); }
//   public override void OnUpdate(float dt) { clock.Tick(GetComponent<AudioSource>()); }
// Judging a press: clock.NearestBeat(out double error) gives the beat and how far off (seconds).
public sealed class BeatClock
{
    public BeatClock(float bpm, int beatsPerBar = 4, double offsetSeconds = 0.0, Entity owner = default)
    {
        Bpm = bpm;
        BeatsPerBar = Math.Max(1, beatsPerBar);
        Offset = offsetSeconds;
        Owner = owner;
    }

    public float Bpm;
    public int BeatsPerBar;
    /// <summary>
    /// Seconds into the track of the first beat.
    /// </summary>
    public double Offset;
    /// <summary>
    /// Publishes BeatEvents when set.
    /// </summary>
    public Entity Owner;
    /// <summary>
    /// A playback position this far from the clock is a seek or a loop: the clock jumps to it.
    /// Closer ones are the mixer's block steps, which the clock smooths over.
    /// </summary>
    public double SnapSeconds = 0.1;
    /// <summary>
    /// Beats fired at most per tick after a hitch (the oldest missed ones are skipped).
    /// </summary>
    public int MaxCatchUp = 16;

    /// <summary>
    /// (beat, bar, beat in bar), from beat 0 at Offset.
    /// </summary>
    public event Action<int, int, int>? OnBeat;
    public event Action<int>? OnBar;

    /// <summary>
    /// Seconds into the track, as the clock keeps it.
    /// </summary>
    public double SongTime { get; private set; }
    public double SecondsPerBeat => 60.0 / Math.Max(Bpm, 1e-3f);
    /// <summary>
    /// Beats since the first one (fractional, negative before Offset).
    /// </summary>
    public double BeatPosition => (SongTime - Offset) / SecondsPerBeat;
    public int Beat => (int)Math.Floor(BeatPosition);
    public int Bar => FloorDiv(Beat, BeatsPerBar);
    public int BeatInBar => Beat - Bar * BeatsPerBar;
    /// <summary>
    /// How far into the current beat, 0..1 (a pulse: 1 - BeatPhase).
    /// </summary>
    public float BeatPhase => (float)(BeatPosition - Math.Floor(BeatPosition));

    private double _anchor = double.NaN;    // the last playback position the mixer reported
    private double _sinceAnchor;
    private int _lastFired = -1;

    /// <summary>
    /// Follows an AudioSource: the mixer's position while it plays, held while paused or stopped,
    /// and game time when there's no audio engine (offline, headless).
    /// </summary>
    public void Tick(AudioSource source)
    {
        double position = source.PlaybackSeconds;
        if (position == -1.0)
        {
            return;     // not playing: hold
        }
        Advance(Time.UnscaledDeltaTime, position);
    }

    /// <summary>
    /// Follows game time alone (no track): beats every SecondsPerBeat from when it started.
    /// </summary>
    public void Tick() => Advance(Time.UnscaledDeltaTime, -2.0);

    /// <summary>
    /// One step: InDelta seconds passed; InPlayback is the track's position as the mixer reports it,
    /// or &lt; 0 to advance by InDelta alone.
    /// </summary>
    public void Advance(double deltaSeconds, double playbackSeconds)
    {
        deltaSeconds = Math.Max(deltaSeconds, 0.0);
        if (playbackSeconds < 0.0)
        {
            SongTime += deltaSeconds;
            Fire();
            return;
        }
        // The mixer moves the position in blocks; between them the clock runs on from the last one.
        if (double.IsNaN(_anchor) || playbackSeconds != _anchor)
        {
            _anchor = playbackSeconds;
            _sinceAnchor = 0.0;
        }
        else
        {
            _sinceAnchor += deltaSeconds;
        }
        double target = _anchor + _sinceAnchor;
        if (Math.Abs(target - SongTime) > SnapSeconds)
        {
            // A seek or a loop: jump. Going back re-arms the beats from there (the current one
            // only when it's just begun, so a seek mid-beat doesn't fire it late).
            bool back = target < SongTime;
            SongTime = target;
            if (back)
            {
                _lastFired = BeatPhase < 0.1f ? Beat - 1 : Beat;
            }
        }
        else
        {
            // Never backwards for a block step: beats must not fire twice.
            SongTime = Math.Max(SongTime, target);
        }
        Fire();
    }

    /// <summary>
    /// The beat nearest the clock's time (or InSongTime) and how far from it, in seconds (negative:
    /// early). Rhythm games judge presses with it.
    /// </summary>
    public int NearestBeat(out double errorSeconds) => NearestBeat(SongTime, out errorSeconds);

    public int NearestBeat(double songTime, out double errorSeconds)
    {
        double position = (songTime - Offset) / SecondsPerBeat;
        int beat = (int)Math.Round(position);
        errorSeconds = (position - beat) * SecondsPerBeat;
        return beat;
    }

    private void Fire()
    {
        int now = Beat;
        if (now <= _lastFired)
        {
            return;
        }
        int first = Math.Max(_lastFired + 1, now - MaxCatchUp + 1);
        _lastFired = now;
        for (int beat = Math.Max(first, 0); beat <= now; ++beat)
        {
            int bar = FloorDiv(beat, BeatsPerBar);
            int inBar = beat - bar * BeatsPerBar;
            OnBeat?.Invoke(beat, bar, inBar);
            if (inBar == 0)
            {
                OnBar?.Invoke(bar);
            }
            if (Owner != default)
            {
                EventBus.Publish(new BeatEvent(Owner, beat, bar, inBar));
            }
        }
    }

    private static int FloorDiv(int a, int b) => (int)Math.Floor(a / (double)b);
}
