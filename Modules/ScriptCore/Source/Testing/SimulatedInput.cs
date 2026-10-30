using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ScriptCore;

/// <summary>
/// Timed simulated input for play-mode tests and replays, on top of Input.Simulate (keys held as if
/// pressed until released; actions read them):
///
/// <code>
///    await SimulatedInput.Hold(KeyCode.W, 1.5f);      // walk forward for 1.5 s of game time
///    await SimulatedInput.Tap(KeyCode.E);             // held for one frame
///    await SimulatedInput.Play(SimulatedInput.Recording.Parse("0 +W; 0.5 +Space; 0.6 -Space; 2 -W"));
/// </code>
///
/// A recording is a timeline of key changes in seconds of game time from its start: "+Key" holds,
/// "-Key" releases (KeyCode names). SimulatedInput.Record() captures one from the real keyboard and
/// prints it to the log when disposed, ready to paste into a test.
/// </summary>
public static class SimulatedInput
{
    public readonly record struct Event(float Time, KeyCode Key, bool Held);

    public sealed class Recording : List<Event>
    {
        public void Add(float time, KeyCode key, bool held) => Add(new Event(time, key, held));

        public static Recording Parse(string text)
        {
            var recording = new Recording();
            foreach (var entry in text.Split(new[] { ';', '\n' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                var parts = entry.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length != 2 || !float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out float time) || time < 0f
                    || parts[1].Length < 2 || (parts[1][0] != '+' && parts[1][0] != '-')
                    || !Enum.TryParse(parts[1].Substring(1), true, out KeyCode key) || key == KeyCode.None)
                    throw new FormatException($"'{entry}' isn't \"<seconds> +Key\" or \"<seconds> -Key\"");
                recording.Add(time, key, parts[1][0] == '+');
            }
            recording.Sort((a, b) => a.Time.CompareTo(b.Time));
            return recording;
        }

        /// <inheritdoc/>
        public override string ToString()
        {
            var text = new StringBuilder();
            foreach (var e in this)
            {
                if (text.Length > 0) text.Append("; ");
                text.Append(e.Time.ToString("0.###", CultureInfo.InvariantCulture)).Append(e.Held ? " +" : " -").Append(e.Key);
            }
            return text.ToString();
        }
    }

    /// <summary>
    /// Holds the key for the given game time (at least one frame), then releases it.
    /// </summary>
    public static async Task Hold(KeyCode key, float seconds)
    {
        Input.Simulate(key);
        try
        {
            if (seconds > 0f) await Async.Seconds(seconds);
            else await Async.NextFrame();
        }
        finally
        {
            Input.Simulate(key, false);
        }
    }

    public static Task Tap(KeyCode key) => Hold(key, 0f);

    /// <summary>
    /// Applies each event once the recording's game time reaches it. Keys still held at the end
    /// stay held (end a recording with its releases, or call Input.ReleaseSimulated).
    /// </summary>
    public static async Task Play(IReadOnlyList<Event> recording)
    {
        float time = 0f;
        int next = 0;
        while (next < recording.Count)
        {
            while (next < recording.Count && recording[next].Time <= time)
            {
                Input.Simulate(recording[next].Key, recording[next].Held);
                ++next;
            }
            if (next == recording.Count) break;
            await Async.NextFrame();
            time += Time.DeltaTime;
        }
    }

    /// <summary>
    /// Records the real keyboard's key changes until disposed, then logs the recording.
    /// </summary>
    public static Recorder Record() => new Recorder();

    public sealed class Recorder : IDisposable
    {
        private static readonly KeyCode[] Keys = Enum.GetValues<KeyCode>().Where(k => k != KeyCode.None).Distinct().ToArray();
        private readonly HashSet<KeyCode> _down = new();
        private bool _running = true;
        public Recording Recording { get; } = new();

        internal Recorder() { _ = Run(); }

        private async Task Run()
        {
            float time = 0f;
            while (_running)
            {
                foreach (var key in Keys)
                {
                    bool down = Input.IsKeyDown(key);
                    if (down == _down.Contains(key)) continue;
                    if (down) _down.Add(key); else _down.Remove(key);
                    Recording.Add(time, key, down);
                }
                await Async.NextFrame();
                time += Time.DeltaTime;
            }
        }

        public void Dispose()
        {
            if (!_running) return;
            _running = false;
            Debug.Log($"[SimulatedInput] recording: {Recording}");
        }
    }
}
