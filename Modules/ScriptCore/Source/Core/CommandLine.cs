using System;
using System.Globalization;
using System.Text;

namespace ScriptCore;

/// <summary>
/// The command line the game started with: flags as "--name value". A name works with or without
/// its dashes ("--bots" and "bots" are the same flag). Numbers read in the invariant culture.
///
/// <code>
///    int bots = CommandLine.GetInt("bots", 2);          // --bots 4
///    string name = CommandLine.Get("name", "Player");   // --name Mitch
///    if (CommandLine.Has("autopilot")) ...              // --autopilot
/// </code>
/// </summary>
public static unsafe class CommandLine
{
    /// <summary>Whether the flag is on the command line.</summary>
    public static bool Has(string name)
    {
        if (Engine._api.CommandLine_Has == null || string.IsNullOrEmpty(name)) return false;
        fixed (byte* p = Engine.Utf8(Flag(name), stackalloc byte[Engine.Utf8StackSize])) return Engine._api.CommandLine_Has(p) != 0;
    }

    /// <summary>The value after the flag; fallback when the flag isn't there or another flag follows it.</summary>
    public static string Get(string name, string fallback = "")
    {
        if (Engine._api.CommandLine_Get == null || string.IsNullOrEmpty(name)) return fallback;
        fixed (byte* p = Engine.Utf8(Flag(name), stackalloc byte[Engine.Utf8StackSize]))
        {
            int length = Engine._api.CommandLine_Get(p, null, 0);
            if (length < 0) return fallback;
            var buffer = new byte[length + 1];
            fixed (byte* value = buffer) Engine._api.CommandLine_Get(p, value, buffer.Length);
            return Encoding.UTF8.GetString(buffer, 0, length);
        }
    }

    /// <summary>The value after the flag as a whole number; fallback when it's missing or isn't one.</summary>
    public static int GetInt(string name, int fallback = 0) =>
        int.TryParse(Get(name), NumberStyles.Integer, CultureInfo.InvariantCulture, out int value) ? value : fallback;

    /// <summary>The value after the flag as a number ("0.5"); fallback when it's missing or isn't one.</summary>
    public static float GetFloat(string name, float fallback = 0f) =>
        float.TryParse(Get(name), NumberStyles.Float, CultureInfo.InvariantCulture, out float value) ? value : fallback;

    // "bots" -> "--bots"; a name that starts with a dash is taken as it is.
    internal static string Flag(string name) => name.StartsWith('-') ? name : "--" + name;
}
