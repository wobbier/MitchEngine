using System;
using System.Collections.Generic;

namespace ScriptCore;

// The owner's input for one simulation tick (Net.Command): networked buttons as bits plus four axes.
public struct NetCommand
{
    public int Tick;
    public int Buttons;
    public int PreviousButtons;
    public float Axis0, Axis1, Axis2, Axis3;
    // False when there's no command for the entity this tick (not owned, or none arrived yet).
    public bool Valid;

    public bool IsDown(string action) => Bit(Buttons, action);
    public bool WasPressed(string action) => Bit(Buttons, action) && !Bit(PreviousButtons, action);
    public bool WasReleased(string action) => !Bit(Buttons, action) && Bit(PreviousButtons, action);

    public float Axis(int i) => i switch { 0 => Axis0, 1 => Axis1, 2 => Axis2, 3 => Axis3, _ => 0f };

    private static bool Bit(int buttons, string action)
    {
        int index = Net.ActionIndex(action);
        return index >= 0 && (buttons & (1 << index)) != 0;
    }
}


// NET-6: the session from scripts. Without a session (offline play, the editor) Role is 0 and RPCs
// run locally, so the same script works on both sides. See Engine/Docs/Networking.md.
public static unsafe class Net
{
    public const int RoleNone = 0;
    public const int RoleServer = 1;
    public const int RoleClient = 2;

    public static int Role => Engine._api.Net_GetRole != null ? Engine._api.Net_GetRole() : RoleNone;
    public static bool IsActive => Role != RoleNone;
    public static bool IsServer => Role == RoleServer;
    public static bool IsClient => Role == RoleClient;

    // This client's id (1-255); 0 on the server and offline.
    public static int LocalClientId => Engine._api.Net_GetLocalClientId != null ? Engine._api.Net_GetLocalClientId() : 0;

    // The simulation tick being run (a past one while resimulating).
    public static int Tick => Engine._api.Net_GetTick != null ? Engine._api.Net_GetTick() : 0;

    // True while the client replays its commands after a correction: skip effects and sounds.
    public static bool IsResimulating => Engine._api.Net_IsResimulating != null && Engine._api.Net_IsResimulating() != 0;

    // The connected client ids (server); returns how many, writing at most ids.Length.
    public static int GetClients(Span<int> ids)
    {
        if (Engine._api.Net_GetClients == null) return 0;
        fixed (int* p = ids) return Engine._api.Net_GetClients(p, ids.Length);
    }

    // Allocates; GetClients(Span) doesn't.
    public static int[] Clients
    {
        get
        {
            Span<int> ids = stackalloc int[256];
            int count = Math.Clamp(GetClients(ids), 0, ids.Length);
            return ids.Slice(0, count).ToArray();
        }
    }

    // The client that owns (and predicts) an entity; 0 = the server / nobody.
    public static int GetOwner(Entity entity) => Engine._api.Net_GetOwner != null ? Engine._api.Net_GetOwner(entity) : 0;
    public static void SetOwner(Entity entity, int clientId)
    {
        if (Engine._api.Net_SetOwner != null) Engine._api.Net_SetOwner(entity, clientId);
    }
    public static bool IsOwner(Entity entity)
    {
        if (!IsClient) return false;
        int id = LocalClientId;   // 0 until connected: owns nothing yet
        return id != 0 && GetOwner(entity) == id;
    }

    // On a client: the entity it predicts (its pawn).
    public static bool IsPredicted(Entity entity) => Engine._api.Net_IsPredicted != null && Engine._api.Net_IsPredicted(entity) != 0;

    // The owner's command for the tick being simulated (server: as received; client: its own).
    public static NetCommand Command(Entity entity)
    {
        var command = new NetCommand();
        if (Engine._api.Net_GetCommand == null) return command;
        int buttons, previous, tick;
        float* axes = stackalloc float[4];
        command.Valid = Engine._api.Net_GetCommand(entity, &buttons, &previous, axes, &tick) != 0;
        if (!command.Valid) return command;
        command.Tick = tick;
        command.Buttons = buttons;
        command.PreviousButtons = previous;
        command.Axis0 = axes[0];
        command.Axis1 = axes[1];
        command.Axis2 = axes[2];
        command.Axis3 = axes[3];
        return command;
    }

    // Holds (or releases) a networked action as if its input were down: bots and tests.
    public static void Press(string action, bool held = true)
    {
        int index = ActionIndex(action);
        if (index < 0)
        {
            WarnOnce("Net.Press: '" + action + "' is not a networked action");
            return;
        }
        if (Engine._api.Net_SetActionOverride != null) Engine._api.Net_SetActionOverride(index, held ? (byte)1 : (byte)0);
    }

    // Prediction on this client: mispredictions corrected, ticks that matched, and initial syncs.
    public static int Corrections { get { GetPredictionStats(out int c, out _, out _); return c; } }
    public static int Matches { get { GetPredictionStats(out _, out int m, out _); return m; } }
    public static int InitialSyncs { get { GetPredictionStats(out _, out _, out int s); return s; } }

    private static void GetPredictionStats(out int corrections, out int matches, out int initialSyncs)
    {
        int c = 0, m = 0, s = 0;
        if (Engine._api.Net_GetPredictionStats != null) Engine._api.Net_GetPredictionStats(&c, &m, &s);
        corrections = c;
        matches = m;
        initialSyncs = s;
    }

    // The id an entity is known by on every peer (0: not networked), and back.
    public static int GetNetId(Entity entity) => Engine._api.Net_GetNetId != null ? Engine._api.Net_GetNetId(entity) : 0;
    public static Entity FindEntity(int netId)
    {
        if (netId == 0 || Engine._api.Net_FindEntity == null) return Entity.Null;
        Entity id = Entity.Null;
        Engine._api.Net_FindEntity(netId, &id);
        return id;
    }

    // An RPC's arguments must fit one message (1024 bytes) with its header.
    public const int MaxRpcArgsBytes = 1008;

    // Generated RPC stubs call this. kind: 0 to the server, 1 to the owner, 2 to every client.
    // Offline both sides are local, and an RPC addressed to the side we're on runs here directly.
    public static void SendRpc(Entity entity, int rpcId, int kind, ReadOnlySpan<byte> args, INetScript self)
    {
        if (args.Length > MaxRpcArgsBytes)
        {
            WarnRpcTooLarge($"{self.GetType().Name} RPC {rpcId}");
            return;
        }
        int role = Role;
        if (role == RoleNone || (role == RoleServer && kind == 0))
        {
            self.NetDispatchRpc(rpcId, args);
            return;
        }
        if (role == RoleClient && kind != 0)
        {
            WarnOnce($"{self.GetType().Name}: a client can't send a ClientRpc (id {rpcId}); dropped");
            return;
        }
        if (Engine._api.Net_SendRpc == null) return;
        fixed (byte* p = args) Engine._api.Net_SendRpc(entity, rpcId, kind, p, args.Length);
    }

    // ME-291: a cosmetic event (FX, a sound, a log line) raised from simulation (OnFixedUpdate) that
    // must show once, not again in every resimulation of its tick:
    // - offline: script.OnNetEvent(eventId, value) now;
    // - the server: now, and to every client (as a reserved RPC carrying its tick);
    // - the predicting owner: now, once per (entity, tick, event): resims of the tick skip it, and so
    //   does the server's copy when it arrives;
    // - other clients: when the server's copy arrives.
    // Needs a networked script (one with [Replicated] / [Predicted] state or RPCs) on a NetIdentity.
    public static void PredictedEvent(Script script, int eventId, int value = 0)
    {
        int role = Role;
        if (role == RoleNone)
        {
            script.OnNetEvent(eventId, value);
            return;
        }
        int tick = Tick;
        if (role == RoleServer)
        {
            script.OnNetEvent(eventId, value);
            if (Engine._api.Net_SendRpc == null) return;
            Span<byte> args = stackalloc byte[12];
            var writer = new NetWriter(args);
            writer.WriteInt(tick);
            writer.WriteInt(eventId);
            writer.WriteInt(value);
            fixed (byte* p = args) Engine._api.Net_SendRpc(script.Entity, PredictedEventRpcId, 2, p, args.Length);
            return;
        }
        if (IsPredicted(script.Entity) && Events.TryPresent(script.Entity, tick, eventId))
        {
            script.OnNetEvent(eventId, value);
        }
    }

    // The RPC id PredictedEvent's server copies travel under (C++: kPredictedEventRpcId).
    public const int PredictedEventRpcId = 0x3FFFFFFF;

    internal static readonly NetEventLedger Events = new();

    internal static void ReceivePredictedEvent(Script script, ReadOnlySpan<byte> data)
    {
        var reader = new NetReader(data);
        int tick = reader.ReadInt();
        int eventId = reader.ReadInt();
        int value = reader.ReadInt();
        if (reader.Overflowed || !IsClient) return;
        if (Events.TryPresent(script.Entity, tick, eventId))
        {
            script.OnNetEvent(eventId, value);
        }
    }

    // Generated RPC stubs with string arguments: the arguments didn't fit the RPC buffer.
    public static void WarnRpcTooLarge(string rpc) => WarnOnce($"{rpc}: the arguments are too large for an RPC; dropped");

    // Action name -> networked button bit, -1 when it isn't one. Cached (only a miss allocates);
    // looked up again when the role changes (the session owns the button map).
    private static readonly Dictionary<string, int> _actionIndices = new();
    private static int _actionIndicesRole = -1;
    private static readonly HashSet<string> _warned = new();

    internal static int ActionIndex(string action)
    {
        if (action is null) return -1;
        int role = Role;
        if (role != _actionIndicesRole)
        {
            _actionIndices.Clear();
            _actionIndicesRole = role;
        }
        if (_actionIndices.TryGetValue(action, out int index)) return index;
        if (Engine._api.Net_GetActionIndex == null) return -1;
        fixed (byte* p = Engine.Utf8(action, stackalloc byte[Engine.Utf8StackSize])) index = Engine._api.Net_GetActionIndex(p);
        _actionIndices[action] = index;
        return index;
    }

    private static void WarnOnce(string message)
    {
        if (!_warned.Add(message)) return;
        if (Engine._api.LogWarning != null) Debug.Warning(message);
        else Console.WriteLine(message);
    }

    // Hot reload: script code changed, so forget the lookups and warnings.
    internal static void ResetCaches()
    {
        Events.Clear();
        _actionIndices.Clear();
        _actionIndicesRole = -1;
        _warned.Clear();
    }
}


// ME-291: the predicted events a client has presented, by (entity, tick, event), so a resimulated
// tick or the server's copy doesn't present one twice. Keeps about the last 256 ticks.
public sealed class NetEventLedger
{
    private readonly HashSet<(uint Index, uint Generation, int Tick, int EventId)> _presented = new();
    private int _newestTick = int.MinValue;

    public int Count => _presented.Count;

    // True the first time (present it now), false for a repeat.
    public bool TryPresent(Entity entity, int tick, int eventId)
    {
        if (!_presented.Add((entity.Index, entity.Generation, tick, eventId)))
        {
            return false;
        }
        _newestTick = Math.Max(_newestTick, tick);
        if (_presented.Count > 1024)
        {
            int oldest = _newestTick - 256;
            _presented.RemoveWhere(key => key.Tick < oldest);
        }
        return true;
    }

    public void Clear()
    {
        _presented.Clear();
        _newestTick = int.MinValue;
    }
}
