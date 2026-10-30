using System.Globalization;
using System.Text;
using System.Text.Json;

namespace ScriptCore;

/// <summary>
/// The engine's audio buses, in AudioBus order.
/// </summary>
public enum AudioBus { Master, Music, SFX, UI, Voice }

// The player's settings (PLAY-2), saved in user://Settings.json and applied as they change:
// graphics quality, bus volumes, language, accessibility, rebinds, plus the game's own values.
// Changes save on their own once they settle; Save() writes now.
//
//   Settings.Quality = "Low";                       // a quality preset (HardwareProfiles.json)
//   Settings.SetVolume(AudioBus.Music, 0.5f);
//   Settings.Rebind("Jump", 0, "Keyboard/J");       // binding 0 of the Jump action
//   Settings.SetFloat("MouseSensitivity", 1.5f);    // the game's own: stored as Game.MouseSensitivity
public static unsafe class Settings
{
    /// <summary>
    /// "" = no player caps; else one of QualityPresets.
    /// </summary>
    public static string Quality
    {
        get => GetStringKey("Graphics.Quality");
        set => SetJson("Graphics.Quality", Quote(value ?? ""));
    }

    public static string[] QualityPresets
    {
        get
        {
            string names = ReadString((b, size) => Engine._api.Settings_GetQualityPresets(b, size));
            return names.Length == 0 ? System.Array.Empty<string>() : names.Split('\n');
        }
    }

    /// <summary>
    /// A string table code ("es" reads Assets/UI/Strings/es.json); "" = none.
    /// </summary>
    public static string Language
    {
        get => GetStringKey("Language");
        set => SetJson("Language", Quote(value ?? ""));
    }

    /// <summary>
    /// The UI's text scale, 0.5..3.
    /// </summary>
    public static float TextScale
    {
        get => (float)GetNumberKey("Accessibility.TextScale", 1.0);
        set => SetJson("Accessibility.TextScale", Number(value));
    }

    /// <summary>
    /// No camera shake (the kit's CameraImpulse honours it); games read it for their own sway.
    /// </summary>
    public static bool ReduceMotion
    {
        get => GetJson("Accessibility.ReduceMotion") == "true";
        set => SetJson("Accessibility.ReduceMotion", value ? "true" : "false");
    }

    /// <summary>
    /// PLAY-7: input actions marked Toggle (sprint, crouch) latch on a press instead of being held.
    /// </summary>
    public static bool ToggleHolds
    {
        get => GetJson("Accessibility.ToggleHolds") == "true";
        set => SetJson("Accessibility.ToggleHolds", value ? "true" : "false");
    }

    /// <summary>
    /// PLAY-7: the high-contrast class on every UI document.
    /// </summary>
    public static bool HighContrast
    {
        get => GetJson("Accessibility.HighContrast") == "true";
        set => SetJson("Accessibility.HighContrast", value ? "true" : "false");
    }

    /// <summary>
    /// Captions and subtitles (Captions.Say / Sound), on by default.
    /// </summary>
    public static bool Subtitles
    {
        get => GetJson("Accessibility.Subtitles") != "false";
        set => SetJson("Accessibility.Subtitles", value ? "true" : "false");
    }

    public static bool SpeakerNames
    {
        get => GetJson("Accessibility.SpeakerNames") != "false";
        set => SetJson("Accessibility.SpeakerNames", value ? "true" : "false");
    }

    /// <summary>
    /// Caption text scale, 0.75..2.
    /// </summary>
    public static float SubtitleSize
    {
        get => (float)GetNumberKey("Accessibility.SubtitleSize", 1.0);
        set => SetJson("Accessibility.SubtitleSize", Number(value));
    }

    /// <summary>
    /// The caption backdrop's opacity, 0..1.
    /// </summary>
    public static float SubtitleBackground
    {
        get => (float)GetNumberKey("Accessibility.SubtitleBackground", 0.7);
        set => SetJson("Accessibility.SubtitleBackground", Number(value));
    }

    /// <summary>
    /// The player's volume for a bus, 0..1 (it scales the project's mix).
    /// </summary>
    public static float GetVolume(AudioBus bus) => (float)GetNumberKey("Audio." + bus, 1.0);
    public static void SetVolume(AudioBus bus, float volume) => SetJson("Audio." + bus, Number(volume));

    /// <summary>
    /// Binds binding `binding` of an action (or a composite part: "Up", "Negative"...) to a control
    /// path ("Keyboard/J", "Gamepad/North"). False for an unknown action, binding or control.
    /// </summary>
    public static bool Rebind(string action, int binding, string control, string part = "")
    {
        fixed (byte* a = Engine.Utf8(action, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* p = Engine.Utf8(part ?? "", stackalloc byte[Engine.Utf8StackSize])) fixed (byte* c = Engine.Utf8(control, stackalloc byte[Engine.Utf8StackSize]))
            return Engine._api.Settings_Rebind(a, binding, p, c) != 0;
    }

    /// <summary>
    /// What a prompt shows for a binding: "Space", "Gamepad South", "W/A/S/D".
    /// </summary>
    public static string GetBindingDisplay(string action, int binding = 0)
    {
        var name = Engine.Utf8(action);
        return ReadString((b, size) => { fixed (byte* a = name) return Engine._api.Settings_GetBindingDisplay(a, binding, b, size); });
    }

    public static void ResetBindings()
    {
        Engine._api.Settings_ResetBindings();
    }

    /// <summary>
    /// Writes the settings now. False when the run keeps them in memory (automated runs).
    /// </summary>
    public static bool Save()
    {
        return Engine._api.Settings_Save() != 0;
    }

    /// <summary>
    /// Every setting back to its default, rebinds included.
    /// </summary>
    public static void Reset()
    {
        Engine._api.Settings_Reset();
    }

    /// <summary>
    /// The game's own values, stored as Game.&lt;key&gt;.
    /// </summary>
    public static float GetFloat(string key, float fallback = 0f) => (float)GetNumberKey("Game." + key, fallback);
    public static void SetFloat(string key, float value) => SetJson("Game." + key, Number(value));
    public static int GetInt(string key, int fallback = 0) => (int)System.Math.Round(GetNumberKey("Game." + key, fallback));
    public static void SetInt(string key, int value) => SetJson("Game." + key, value.ToString(CultureInfo.InvariantCulture));
    public static bool GetBool(string key, bool fallback = false)
    {
        string json = GetJson("Game." + key);
        return json == "true" || (json != "false" && fallback);
    }
    public static void SetBool(string key, bool value) => SetJson("Game." + key, value ? "true" : "false");
    public static string GetString(string key, string fallback = "")
    {
        return GetStringKey("Game." + key, fallback);
    }
    public static void SetString(string key, string value) => SetJson("Game." + key, Quote(value ?? ""));

    /// <summary>
    /// Any key as JSON text ("null" when it isn't set).
    /// </summary>
    public static string GetJson(string key)
    {
        var k = Engine.Utf8(key);
        return ReadString((b, size) => { fixed (byte* kp = k) return Engine._api.Settings_Get(kp, b, size); });
    }

    /// <summary>
    /// False (and an error in the log) when the key or the value is rejected.
    /// </summary>
    public static bool SetJson(string key, string json)
    {
        fixed (byte* k = Engine.Utf8(key, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* v = Engine.Utf8(json, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Settings_Set(k, v) != 0;
    }

    private static string GetStringKey(string key, string fallback = "")
    {
        try
        {
            using var doc = JsonDocument.Parse(GetJson(key));
            return doc.RootElement.ValueKind == JsonValueKind.String ? doc.RootElement.GetString() ?? fallback : fallback;
        }
        catch (JsonException) { return fallback; }
    }

    private static double GetNumberKey(string key, double fallback)
    {
        string json = GetJson(key);
        return double.TryParse(json, NumberStyles.Float, CultureInfo.InvariantCulture, out double value) ? value : fallback;
    }

    private static string Number(float value) => ((double)value).ToString("R", CultureInfo.InvariantCulture);

    // JSON string text (JsonEncodedText escapes without reflection: AOT-safe).
    private static string Quote(string value) => "\"" + JsonEncodedText.Encode(value) + "\"";

    private delegate int Reader(byte* buffer, int size);

    // Calls InRead with a buffer, growing it when the engine reports a longer value.
    private static string ReadString(Reader read)
    {
        var buffer = new byte[256];
        int length;
        fixed (byte* b = buffer) length = read(b, buffer.Length);
        if (length >= buffer.Length)
        {
            buffer = new byte[length + 1];
            fixed (byte* b = buffer) length = read(b, buffer.Length);
        }
        return Encoding.UTF8.GetString(buffer, 0, System.Math.Clamp(length, 0, buffer.Length - 1));
    }
}
