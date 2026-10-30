using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Text.Json;

namespace ScriptCore;

// Data assets (CS-7): a C# class whose public fields live in an .asset file, for tuning data shared
// by many scripts (weapon stats, level settings, dialogue). Make one with Create > Data Asset in
// the asset browser, edit it there, and point scripts at it with an AssetRef:
//
//   public class WeaponStats : DataAsset { public float Damage = 10f; public int Ammo = 30; }
//   [AssetFilter("asset")] public AssetRef Stats;   ...   var stats = Stats.Load<WeaponStats>();
//
// The file is { "Type": "WeaponStats", "Fields": { ... } }. Loads are cached per path, so every
// script shares one instance; when the file changes while playing, that instance re-reads in place
// (OnLoaded runs again), so holders see the new values. A hot reload of the scripts drops the cache.
public abstract class DataAsset
{
    public string AssetPath { get; internal set; } = "";

    /// <summary>
    /// After the values (re)load: on first load and after the file changes.
    /// </summary>
    public virtual void OnLoaded() { }
}

public static class DataAssets
{
    private static readonly Dictionary<string, DataAsset> Cache = new();
    private static readonly List<Type> Types = new();

    public static T? Load<T>(string path) where T : DataAsset => Load(path) as T;

    public static DataAsset? Load(string path)
    {
        if (string.IsNullOrEmpty(path)) return null;
        if (Cache.TryGetValue(path, out var cached)) return cached;
        if (!Read(path, out string type, out string fields)) return null;
        var t = FindType(type);
        if (t == null)
        {
            Engine.Log($"[DataAsset] {path}: no DataAsset class named '{type}'");
            return null;
        }
        var asset = (DataAsset)Activator.CreateInstance(t)!;
        asset.AssetPath = path;
        ScriptSerializer.Deserialize(asset, fields);
        Cache[path] = asset;
        asset.OnLoaded();
        return asset;
    }

    /// <summary>
    /// Writes the asset's fields back to its file.
    /// </summary>
    public static bool Save(DataAsset asset)
    {
        string json = "{\"Type\": " + JsonSerializer.Serialize(asset.GetType().Name) + ", \"Fields\": " + ScriptSerializer.Serialize(asset) + "}";
        using var doc = JsonDocument.Parse(json);
        string pretty = JsonSerializer.Serialize(doc.RootElement, new JsonSerializerOptions { WriteIndented = true });
        unsafe { fixed (byte* p = Engine.Utf8(asset.AssetPath, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* t = Engine.Utf8(pretty, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Asset_WriteText(p, t) != 0; }
    }

    public static IReadOnlyList<Type> AssetTypes => Types;

    // append: a mod's assembly (MOD-2) adds to the game's types.
    internal static void Scan(Assembly assembly, bool append = false)
    {
        if (!append) Types.Clear();
        foreach (var t in assembly.GetTypes())
        {
            if (!t.IsAbstract && typeof(DataAsset).IsAssignableFrom(t)) Types.Add(t);
        }
        Types.Sort((a, b) => string.CompareOrdinal(a.Name, b.Name));
    }

    // The file changed: the cached instance re-reads in place.
    internal static void Refresh(string path, bool log = true)
    {
        if (!Cache.TryGetValue(path, out var asset) || !Read(path, out _, out string fields)) return;
        ScriptSerializer.Deserialize(asset, fields);
        asset.OnLoaded();
        if (log) Engine.Log($"[DataAsset] reloaded {path}");
    }

    internal static void Clear() => Cache.Clear();

    private static Type? FindType(string name)
    {
        foreach (var t in Types) if (t.Name == name) return t;
        return null;
    }

    private static unsafe bool Read(string path, out string type, out string fields)
    {
        type = ""; fields = "{}";
        var buffer = new byte[4096];
        int length;
        fixed (byte* p = Engine.Utf8(path, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* b = buffer) length = Engine._api.Asset_ReadText(p, b, buffer.Length);
        if (length < 0) return false;
        if (length >= buffer.Length)
        {
            buffer = new byte[length + 1];
            fixed (byte* p = Engine.Utf8(path, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* b = buffer) length = Engine._api.Asset_ReadText(p, b, buffer.Length);
        }
        try
        {
            using var doc = JsonDocument.Parse(Encoding.UTF8.GetString(buffer, 0, Math.Clamp(length, 0, buffer.Length - 1)));
            var root = doc.RootElement;
            type = root.TryGetProperty("Type", out var t) && t.ValueKind == JsonValueKind.String ? t.GetString() ?? "" : "";
            fields = root.TryGetProperty("Fields", out var f) ? f.GetRawText() : "{}";
            return true;
        }
        catch (JsonException)
        {
            Engine.Log($"[DataAsset] {path} isn't valid JSON");
            return false;
        }
    }

    // The editor's asset details pane: the asset's fields as property rows; saved when edited.
    internal static void Inspect(string path)
    {
        // The file is the truth: it may have changed while nothing was playing to hear about it.
        Refresh(path, log: false);
        var asset = Load(path);
        if (asset == null)
        {
            Engine.Log($"[DataAsset] can't inspect {path}");
            return;
        }
        if (Inspector.DrawObjectFieldsChanged(asset))
        {
            Save(asset);
            asset.OnLoaded();
        }
    }
}
