using System;
using System.Collections.Generic;

namespace ScriptCore;

// PLAY-7: subtitles and closed captions in the stock caption strip (Engine/Assets/UI/Captions/
// Captions.rml, a UIDocument the scene adds; it binds the "captions" model). The player's
// Accessibility settings decide whether they show, whether speakers are named, their size and
// their backdrop:
//   Captions.Say("Guard", "Who goes there?");      // a line of speech, for as long as it takes to read
//   Captions.Sound("door creaks");                 // a sound, shown as [door creaks]
// Up to three lines show at once, newest last; each goes after its time (real time, so a paused
// game doesn't hold them forever).
public static class Captions
{
    public const string Model = "captions";
    private static readonly CaptionLines _lines = new();

    public static IReadOnlyList<CaptionLines.Line> Lines => _lines.Current;

    public static void Say(string speaker, string text, float seconds = 0f)
    {
        if (!Settings.Subtitles || string.IsNullOrEmpty(text)) return;
        Show(_lines.Add(speaker ?? "", text, seconds, Time.RealTime));
    }

    public static void Sound(string description, float seconds = 2.5f)
    {
        if (!Settings.Subtitles || string.IsNullOrEmpty(description)) return;
        Show(_lines.Add("", "[" + description + "]", seconds, Time.RealTime));
    }

    public static void Clear()
    {
        _lines.Clear();
        Refresh();
    }

    // Drops the lines whose time is up and writes the model (Say and Sound schedule it).
    public static void Refresh()
    {
        _lines.Expire(Time.RealTime);
        var model = UI.Model(Model);
        // The style values first: a document that binds as the model appears needs them to exist.
        model.Set("fontsize", $"{MathF.Round(22f * Math.Clamp(Settings.SubtitleSize, 0.75f, 2f))}dp");
        int alpha = (int)MathF.Round(255f * Math.Clamp(Settings.SubtitleBackground, 0f, 1f));
        model.Set("backdrop", $"#000000{alpha:X2}");
        bool names = Settings.SpeakerNames;
        model.Set("visible", _lines.Current.Count > 0 && Settings.Subtitles);
        for (int i = 0; i < CaptionLines.MaxLines; ++i)
        {
            bool has = i < _lines.Current.Count;
            var line = has ? _lines.Current[i] : default;
            model.Set("has" + i, has);
            model.Set("line" + i, has ? line.Text : "");
            model.Set("speaker" + i, has ? line.Speaker : "");
            model.Set("hasSpeaker" + i, has && names && line.Speaker.Length > 0);
        }
    }

    [ConsoleCommand("captions", "Shows captions: captions say <speaker> | <text>, captions sound <text>, captions clear, captions refresh")]
    public static string Command(string[] args)
    {
        string verb = args.Length > 0 ? args[0] : "";
        string rest = string.Join(" ", args, Math.Min(1, args.Length), Math.Max(args.Length - 1, 0));
        switch (verb)
        {
            case "say":
            {
                int bar = rest.IndexOf('|');
                if (bar < 0) Say("", rest.Trim()); else Say(rest[..bar].Trim(), rest[(bar + 1)..].Trim());
                return "said";
            }
            case "sound": Sound(rest.Trim()); return "captioned";
            case "clear": Clear(); return "cleared";
            case "refresh": Refresh(); return $"{_lines.Current.Count} line(s)";
            default: return "captions say <speaker> | <text> | sound <text> | clear | refresh";
        }
    }

    private static void Show(float seconds)
    {
        Refresh();
        Timer.After(seconds + 0.05f, Refresh);
    }
}

// The caption queue itself: no engine calls, so tests can drive it with their own clock.
public sealed class CaptionLines
{
    public const int MaxLines = 3;
    public readonly record struct Line(string Speaker, string Text, float Until);
    private readonly List<Line> _lines = new();

    public IReadOnlyList<Line> Current => _lines;

    // About as long as it takes to read: 1.5 s plus 60 ms a character, 2 to 8 s.
    public static float ReadingSeconds(string text) => Math.Clamp(1.5f + text.Length * 0.06f, 2f, 8f);

    // Returns how long the line shows.
    public float Add(string speaker, string text, float seconds, float now)
    {
        float length = seconds > 0f ? seconds : ReadingSeconds(text);
        _lines.Add(new Line(speaker, text, now + length));
        while (_lines.Count > MaxLines) _lines.RemoveAt(0);
        return length;
    }

    public void Expire(float now) => _lines.RemoveAll(l => l.Until <= now);

    public void Clear() => _lines.Clear();
}
