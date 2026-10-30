using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;

namespace ScriptCore;

/// <summary>
/// Saves a non-public field with save games (public fields always are). Scenes never store it.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class SaveAttribute : Attribute { }

/// <summary>A save game's details (SaveGame.List).</summary>
public readonly struct SaveSlot
{
    /// <summary>The slot's name (its folder under user://Saves).</summary>
    public readonly string Slot;
    /// <summary>The display name given to Save.</summary>
    public readonly string Name;
    /// <summary>The scene that was saved.</summary>
    public readonly string Scene;
    /// <summary>UTC, ISO 8601</summary>
    public readonly string Date;
    /// <summary>Seconds</summary>
    public readonly double Playtime;

    internal SaveSlot(string slot, string name, string scene, string date, double playtime)
    {
        Slot = slot; Name = name; Scene = scene; Date = date; Playtime = playtime;
    }
}

/// <summary>
/// Save games (PLAY-4): the whole scene (transforms, components, script fields, [Save] fields) in a
/// named slot under user://Saves, with the date, playtime and a thumbnail. Saving writes on a worker;
/// loading replaces the scene at the end of the frame, so call it from anywhere.
///
/// <code>
///    SaveGame.Save("slot1", "Chapter 2");
///    foreach (var slot in SaveGame.List()) ...    // newest first
///    SaveGame.Load("slot1");
/// </code>
/// </summary>
public static unsafe class SaveGame
{
    /// <summary>
    /// Slot names are letters, digits, '-' and '_'. False when the name is invalid or there's no scene.
    /// </summary>
    public static bool Save(string slot, string name = "")
    {
        fixed (byte* s = Engine.Utf8(slot, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* n = Engine.Utf8(name ?? "", stackalloc byte[Engine.Utf8StackSize])) return Engine._api.SaveGame_Save(s, n) != 0;
    }

    /// <summary>Loads a slot (the scene is replaced at the end of the frame); false when it doesn't exist.</summary>
    public static bool Load(string slot)
    {
        fixed (byte* s = Engine.Utf8(slot, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.SaveGame_Load(s) != 0;
    }

    /// <summary>Deletes a slot; false when it doesn't exist.</summary>
    public static bool Delete(string slot)
    {
        fixed (byte* s = Engine.Utf8(slot, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.SaveGame_Delete(s) != 0;
    }

    /// <summary>Whether a slot exists.</summary>
    public static bool Exists(string slot)
    {
        foreach (var entry in List())
        {
            if (entry.Slot == slot) return true;
        }
        return false;
    }

    /// <summary>Every save game, newest first.</summary>
    public static SaveSlot[] List()
    {
        var buffer = new byte[4096];
        int length;
        fixed (byte* b = buffer) length = Engine._api.SaveGame_List(b, buffer.Length);
        if (length >= buffer.Length)
        {
            buffer = new byte[length + 1];
            fixed (byte* b = buffer) length = Engine._api.SaveGame_List(b, buffer.Length);
        }
        var slots = new List<SaveSlot>();
        try
        {
            using var doc = JsonDocument.Parse(Encoding.UTF8.GetString(buffer, 0, Math.Clamp(length, 0, buffer.Length - 1)));
            foreach (var e in doc.RootElement.EnumerateArray())
            {
                slots.Add(new SaveSlot(Text(e, "Slot"), Text(e, "Name"), Text(e, "Scene"), Text(e, "Date"),
                    e.TryGetProperty("Playtime", out var p) && p.ValueKind == JsonValueKind.Number ? p.GetDouble() : 0.0));
            }
        }
        catch (JsonException) { }
        return slots.ToArray();
    }

    /// <summary>
    /// True while a save is still being written.
    /// </summary>
    public static bool IsWriting => Engine._api.SaveGame_IsWriting() != 0;

    /// <summary>
    /// Seconds played, counting from the loaded save's playtime.
    /// </summary>
    public static float Playtime => Engine._api.SaveGame_GetPlaytime();

    private static string Text(JsonElement e, string key) =>
        e.TryGetProperty(key, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
}
