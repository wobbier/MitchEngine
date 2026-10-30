using System;
using System.Text;

namespace ScriptCore;

// Localization (PLAY-6): text by key from the string tables (Assets/Localization/<code>.json over
// the engine's). A key with no translation comes back as itself, so English keys read fine before
// anything is translated. "qps" is the pseudo-locale: every known string accented and padded.
//
//   label.Text = Loc.Get("Press a key...");
//   Loc.Language = "es";                     // a player setting: saved, and documents reload
public static unsafe class Loc
{
    public static string Get(string key) => Read((b, n) => { fixed (byte* k = Engine.Utf8(key, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Loc_Get(k, b, n); });

    /// <summary>
    /// "" = none. Setting it changes the player's Language setting.
    /// </summary>
    public static string Language
    {
        get => Read((b, n) => Engine._api.Loc_GetLanguage(b, n));
        set => Settings.Language = value ?? "";
    }

    public static string[] Languages
    {
        get
        {
            string codes = Read((b, n) => Engine._api.Loc_GetLanguages(b, n));
            return codes.Length == 0 ? Array.Empty<string>() : codes.Split('\n');
        }
    }

    private delegate int Reader(byte* buffer, int size);

    private static string Read(Reader read)
    {
        var buffer = new byte[256];
        int length;
        fixed (byte* b = buffer) length = read(b, buffer.Length);
        if (length >= buffer.Length)
        {
            buffer = new byte[length + 1];
            fixed (byte* b = buffer) length = read(b, buffer.Length);
        }
        return Encoding.UTF8.GetString(buffer, 0, Math.Clamp(length, 0, buffer.Length - 1));
    }
}
