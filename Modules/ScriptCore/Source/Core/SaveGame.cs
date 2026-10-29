using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;

namespace ScriptCore;

// Saves a non-public field with save games (public fields always are). Scenes never store it.
[AttributeUsage(AttributeTargets.Field)]
public sealed class SaveAttribute : Attribute { }

public readonly struct SaveSlot
{
    public readonly string Slot;
    public readonly string Name;
    public readonly string Scene;
    public readonly string Date;       // UTC, ISO 8601
    public readonly double Playtime;   // seconds

    internal SaveSlot(string slot, string name, string scene, string date, double playtime)
    {
        Slot = slot; Name = name; Scene = scene; Date = date; Playtime = playtime;
    }
}

// Save games (PLAY-4): the whole scene (transforms, components, script fields, [Save] fields) in a
// named slot under user://Saves, with the date, playtime and a thumbnail. Saving writes on a worker;
// loading replaces the scene at the end of the frame, so call it from anywhere.
//
//   SaveGame.Save("slot1", "Chapter 2");
//   foreach (var slot in SaveGame.List()) ...    // newest first
//   SaveGame.Load("slot1");
public static unsafe class SaveGame
{
    // Slot names are letters, digits, '-' and '_'. False when the name is invalid or there's no scene.
    public static bool Save(string slot, string name = "")
    {
        fixed (byte* s = Engine.Utf8(slot)) fixed (byte* n = Engine.Utf8(name ?? "")) return Engine._api.SaveGame_Save(s, n) != 0;
    }

    public static bool Load(string slot)
    {
        fixed (byte* s = Engine.Utf8(slot)) return Engine._api.SaveGame_Load(s) != 0;
    }

    public static bool Delete(string slot)
    {
        fixed (byte* s = Engine.Utf8(slot)) return Engine._api.SaveGame_Delete(s) != 0;
    }

    public static bool Exists(string slot)
    {
        foreach (var entry in List())
        {
            if (entry.Slot == slot) return true;
        }
        return false;
    }

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

    // True while a save is still being written.
    public static bool IsWriting => Engine._api.SaveGame_IsWriting() != 0;

    // Seconds played, counting from the loaded save's playtime.
    public static float Playtime => Engine._api.SaveGame_GetPlaytime();

    private static string Text(JsonElement e, string key) =>
        e.TryGetProperty(key, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
}
