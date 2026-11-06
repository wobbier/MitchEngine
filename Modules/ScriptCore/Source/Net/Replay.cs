using System;

namespace ScriptCore;

/// <summary>
/// Match replays (ME-316): a server started with --record-replay records every tick of the match
/// (every networked entity, each client's command and link, lag-compensated shots). Replay.Event adds
/// the game's own moments at the current tick (kills, picks, damage, respawns), so a replay dump
/// (<c>Player --replay-dump file.mreplay --events</c>) and the viewer can find them. Nothing happens
/// unless this process records: a client or an offline game can call it freely.
///
/// <code>
///    Replay.Event("kill", $"{{\"killer\":\"{killer}\",\"victim\":\"{victim}\"}}", victimPawn);
/// </code>
/// </summary>
public static unsafe class Replay
{
    /// <summary>Whether this process records a match replay (a server with --record-replay).</summary>
    public static bool IsRecording => Engine._api.Replay_IsRecording != null && Engine._api.Replay_IsRecording() != 0;

    /// <summary>
    /// An event at the current tick: a short name (up to 64 bytes) and a payload (JSON by convention,
    /// up to 4000 bytes; longer is cut). Nothing unless recording.
    /// </summary>
    public static void Event(string name, string payload = "") => Event(name, payload, Entity.Null);

    /// <summary>
    /// An event about an entity (its NetId goes in the replay; --entity filters on it). Nothing unless recording.
    /// </summary>
    public static void Event(string name, string payload, Entity entity)
    {
        if (Engine._api.Replay_Event == null || string.IsNullOrEmpty(name) || !IsRecording)
        {
            return;
        }
        fixed (byte* n = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize]))
        fixed (byte* p = Engine.Utf8(payload ?? string.Empty, stackalloc byte[Engine.Utf8StackSize]))
        {
            Engine._api.Replay_Event(n, p, entity);
        }
    }

    /// <summary>A JSON string literal for a payload: quotes, backslashes and control characters escaped.</summary>
    public static string Quote(string text)
    {
        var builder = new System.Text.StringBuilder(text.Length + 2);
        builder.Append('"');
        foreach (char c in text)
        {
            switch (c)
            {
                case '"': builder.Append("\\\""); break;
                case '\\': builder.Append("\\\\"); break;
                case '\n': builder.Append("\\n"); break;
                case '\r': builder.Append("\\r"); break;
                case '\t': builder.Append("\\t"); break;
                default:
                    if (c < ' ') builder.Append("\\u").Append(((int)c).ToString("x4"));
                    else builder.Append(c);
                    break;
            }
        }
        return builder.Append('"').ToString();
    }
}
