using System;
using System.Collections.Generic;
using System.Text.Json;

namespace ScriptCore;

// Typed events between scripts, plus the engine's own (CS-8). Any type can be an event:
//
//   public readonly record struct CoinCollected(int Value);
//   EventBus.Publish(new CoinCollected(5));
//   EventBus.Subscribe<CoinCollected>(c => score += c.Value);    // returns a token: Dispose to stop
//   On<SceneLoaded>(e => Debug.Log(e.Path));                    // in a Script: dropped when it's destroyed
//
// Publish delivers at once, in subscription order; a throwing handler is reported and the rest
// still run. A script's subscriptions go when it's destroyed, and every subscription goes on a
// hot reload (scripts subscribe again in OnStart / OnReload). Engine events arrive once a frame,
// before the scripts' OnUpdate.
public static class EventBus
{
    private static readonly Dictionary<Type, List<Delegate>> Handlers = new();

    public static IDisposable Subscribe<T>(Action<T> handler)
    {
        if (!Handlers.TryGetValue(typeof(T), out var list))
        {
            Handlers[typeof(T)] = list = new List<Delegate>();
        }
        list.Add(handler);
        return new Subscription(() => Unsubscribe(handler));
    }

    public static void Unsubscribe<T>(Action<T> handler)
    {
        if (Handlers.TryGetValue(typeof(T), out var list))
        {
            list.Remove(handler);
        }
    }

    public static void Publish<T>(T evt)
    {
        if (!Handlers.TryGetValue(typeof(T), out var list) || list.Count == 0) return;
        // A copy: handlers may subscribe or unsubscribe while it's delivered.
        foreach (var handler in list.ToArray())
        {
            try { ((Action<T>)handler)(evt); }
            catch (Exception ex) { ScriptBridge.ReportHandlerException(typeof(T).Name, ex); }
        }
    }

    public static int SubscriberCount<T>() => Handlers.TryGetValue(typeof(T), out var list) ? list.Count : 0;

    // A wrapper handler (a filter) that belongs to the script its inner delegate targets.
    internal interface IOwnedHandler
    {
        object? Owner { get; }
    }

    // A destroyed script's handlers (delegates whose target is it, or a wrapper it owns).
    internal static void RemoveTarget(object target)
    {
        foreach (var list in Handlers.Values)
        {
            list.RemoveAll(d => ReferenceEquals(d.Target, target) || (d.Target is IOwnedHandler owned && ReferenceEquals(owned.Owner, target)));
        }
    }

    // Hot reload: handlers of the old game assembly would pin it.
    internal static void Clear() => Handlers.Clear();

    // The engine's JSON array of events (ScriptCore queues them), as typed events.
    internal static void DispatchEngine(string json)
    {
        using var doc = JsonDocument.Parse(json);
        foreach (var e in doc.RootElement.EnumerateArray())
        {
            switch (Text(e, "Type"))
            {
                case "SceneLoaded": Publish(new SceneLoaded(Text(e, "Path"))); break;
                case "AdditiveScene": Publish(new AdditiveSceneChanged(Int(e, "SceneId"), Text(e, "Path"), Bool(e, "Loaded"))); break;
                case "Gamepad": Publish(new GamepadChanged(Int(e, "Slot"), Bool(e, "Connected"), Text(e, "Name"))); break;
                case "Player": Publish(new PlayerChanged(Int(e, "Player"), Int(e, "GamepadSlot"), (PlayerChange)Int(e, "Change"))); break;
                case "Animation":
                {
                    var id = e.GetProperty("Entity");
                    Publish(new AnimationMarker(new Entity(id[0].GetUInt32(), id[1].GetUInt32()), Text(e, "State"), Text(e, "Name")));
                    break;
                }
                case "Sequence":
                {
                    var e2 = new SequenceEvent(EntityOf(e, "Player"), EntityOf(e, "Receiver"), (SequenceEventKind)Int(e, "Kind"), Text(e, "Name"), Text(e, "Payload"),
                        e.TryGetProperty("Number", out var n) && n.ValueKind == JsonValueKind.Number ? n.GetSingle() : 0f, Int(e, "Frame"), Text(e, "Clip"));
                    if (!e2.Receiver.Equals(Entity.Null) && ScriptBridge.FindScript<ISequenceReceiver>(e2.Receiver) is { } receiver)
                    {
                        try { receiver.OnSequenceEvent(e2); }
                        catch (Exception ex) { ScriptBridge.ReportHandlerException(nameof(ISequenceReceiver), ex); }
                    }
                    Publish(e2);
                    break;
                }
                case "AssetsChanged":
                {
                    var paths = new List<string>();
                    foreach (var p in e.GetProperty("Paths").EnumerateArray()) paths.Add(p.GetString() ?? "");
                    foreach (var path in paths) if (path.EndsWith(".asset", StringComparison.OrdinalIgnoreCase)) DataAssets.Refresh(path);
                    Publish(new AssetsChanged(paths.ToArray()));
                    break;
                }
                case "WindowResized":
                {
                    var size = e.GetProperty("Size");
                    Publish(new WindowResized(new Vector2(size[0].GetSingle(), size[1].GetSingle())));
                    break;
                }
            }
        }
    }

    private static Entity EntityOf(JsonElement e, string key)
    {
        if (!e.TryGetProperty(key, out var id) || id.ValueKind != JsonValueKind.Array || id.GetArrayLength() != 2) return Entity.Null;
        return new Entity(id[0].GetUInt32(), id[1].GetUInt32());
    }
    private static string Text(JsonElement e, string key) => e.TryGetProperty(key, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
    private static int Int(JsonElement e, string key) => e.TryGetProperty(key, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetInt32() : 0;
    private static bool Bool(JsonElement e, string key) => e.TryGetProperty(key, out var v) && v.ValueKind == JsonValueKind.True;

    private sealed class Subscription : IDisposable
    {
        private Action? _unsubscribe;
        public Subscription(Action unsubscribe) { _unsubscribe = unsubscribe; }
        public void Dispose() { _unsubscribe?.Invoke(); _unsubscribe = null; }
    }
}

/// <summary>
/// The main scene finished loading (its scripts start this frame).
/// </summary>
public readonly record struct SceneLoaded(string Path);
/// <summary>
/// An additive scene (World.LoadSceneAdditive) loaded, or unloaded (Loaded false).
/// </summary>
public readonly record struct AdditiveSceneChanged(int SceneId, string Path, bool Loaded);
/// <summary>
/// A pad connected to or left a slot.
/// </summary>
public readonly record struct GamepadChanged(int Slot, bool Connected, string Name);
public enum PlayerChange { Joined, DeviceLost, DeviceRegained }
/// <summary>
/// A local player joined, lost its pad or got it back.
/// </summary>
public readonly record struct PlayerChanged(int Player, int GamepadSlot, PlayerChange Change);
/// <summary>
/// An Animator passed a named marker in a state.
/// </summary>
public readonly record struct AnimationMarker(Entity Entity, string State, string Name);
public readonly record struct WindowResized(Vector2 Size);
/// <summary>
/// Project files changed on disk while playing (tools builds): project-relative paths.
/// </summary>
public readonly record struct AssetsChanged(string[] Paths);
