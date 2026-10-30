using System;
using System.Collections.Generic;

namespace ScriptCore;

/// <summary>
/// The owner's input for one simulation tick (Net.Command): networked buttons as bits plus four axes.
/// </summary>
public struct NetCommand
{
    /// <summary>The simulation tick this command is for.</summary>
    public int Tick;
    /// <summary>The networked button actions held, one bit each (IsDown reads them by name).</summary>
    public int Buttons;
    /// <summary>The previous command's buttons (for WasPressed / WasReleased).</summary>
    public int PreviousButtons;
    /// <summary>The networked Value / Vector2 actions, -1..1 at 16 bits.</summary>
    public float Axis0, Axis1, Axis2, Axis3;
    /// <summary>The (fractional) server tick the owner's view showed when it sent this command (NET-8); Net.LagCompensatedRaycast rewinds to it. -1 without one.</summary>
    public double ViewTick;
    /// <summary>
    /// The owner's absolute aim when it sent this command (NET-15; Net.SetAim), in degrees: Yaw about
    /// +Y (0 looking down +Z, 90 down +X), Pitch up from level. HasAim is false until it sets one.
    /// </summary>
    public bool HasAim;
    /// <summary>The aim in degrees (see HasAim).</summary>
    public float Yaw, Pitch;

    /// <summary>
    /// The aim as a unit direction (+Z when there's none).
    /// </summary>
    public Vector3 AimDirection
    {
        get
        {
            float yaw = Yaw * (MathF.PI / 180f), pitch = Pitch * (MathF.PI / 180f);
            return new Vector3(MathF.Sin(yaw) * MathF.Cos(pitch), MathF.Sin(pitch), MathF.Cos(yaw) * MathF.Cos(pitch));
        }
    }
    /// <summary>
    /// False when there's no command for the entity this tick (not owned, or none arrived yet).
    /// </summary>
    public bool Valid;

    /// <summary>Whether a networked button action is held in this command.</summary>
    public bool IsDown(string action) => Bit(Buttons, action);
    /// <summary>Whether it went down this tick.</summary>
    public bool WasPressed(string action) => Bit(Buttons, action) && !Bit(PreviousButtons, action);
    /// <summary>Whether it went up this tick.</summary>
    public bool WasReleased(string action) => !Bit(Buttons, action) && Bit(PreviousButtons, action);

    /// <summary>An axis by index (0..3).</summary>
    public float Axis(int i) => i switch { 0 => Axis0, 1 => Axis1, 2 => Axis2, 3 => Axis3, _ => 0f };

    private static bool Bit(int buttons, string action)
    {
        int index = Net.ActionIndex(action);
        return index >= 0 && (buttons & (1 << index)) != 0;
    }
}


/// <summary>
/// Net.LagCompensatedRaycast's result. Entity: the hitbox's entity (also when Occluded hid it).
/// Rejected: the shooter's view was in the future. Rewound: judged at the shooter's view (server).
/// </summary>
public readonly record struct NetHit(bool Hit, Entity Entity, Vector3 Point, bool Occluded, bool Rejected, bool Clamped, bool Rewound);


/// <summary>
/// NET-6: the session from scripts. Without a session (offline play, the editor) Role is 0 and RPCs
/// run locally, so the same script works on both sides. See Engine/Docs/Networking.md.
/// </summary>
public static unsafe class Net
{
    /// <summary>No session.</summary>
    public const int RoleNone = 0;
    /// <summary>This peer is the server.</summary>
    public const int RoleServer = 1;
    /// <summary>This peer is a client.</summary>
    public const int RoleClient = 2;

    /// <summary>This peer's role: RoleNone, RoleServer or RoleClient.</summary>
    public static int Role => Engine._api.Net_GetRole != null ? Engine._api.Net_GetRole() : RoleNone;
    /// <summary>Whether there's a session.</summary>
    public static bool IsActive => Role != RoleNone;
    /// <summary>Whether this peer is the server.</summary>
    public static bool IsServer => Role == RoleServer;
    /// <summary>Whether this peer is a client.</summary>
    public static bool IsClient => Role == RoleClient;

    /// <summary>
    /// This client's id (1-255); 0 on the server and offline.
    /// </summary>
    public static int LocalClientId => Engine._api.Net_GetLocalClientId != null ? Engine._api.Net_GetLocalClientId() : 0;

    /// <summary>
    /// The simulation tick being run (a past one while resimulating).
    /// </summary>
    public static int Tick => Engine._api.Net_GetTick != null ? Engine._api.Net_GetTick() : 0;

    /// <summary>
    /// True while the client replays its commands after a correction: skip effects and sounds.
    /// </summary>
    public static bool IsResimulating => Engine._api.Net_IsResimulating != null && Engine._api.Net_IsResimulating() != 0;

    /// <summary>
    /// The connected client ids (server); returns how many, writing at most ids.Length.
    /// </summary>
    public static int GetClients(Span<int> ids)
    {
        if (Engine._api.Net_GetClients == null) return 0;
        fixed (int* p = ids) return Engine._api.Net_GetClients(p, ids.Length);
    }

    /// <summary>
    /// Allocates; GetClients(Span) doesn't.
    /// </summary>
    public static int[] Clients
    {
        get
        {
            Span<int> ids = stackalloc int[256];
            int count = Math.Clamp(GetClients(ids), 0, ids.Length);
            return ids.Slice(0, count).ToArray();
        }
    }

    /// <summary>
    /// The client that owns (and predicts) an entity; 0 = the server / nobody.
    /// </summary>
    public static int GetOwner(Entity entity) => Engine._api.Net_GetOwner != null ? Engine._api.Net_GetOwner(entity) : 0;
    /// <summary>Server: hands an entity to a client, which then predicts it (0: back to the server).</summary>
    public static void SetOwner(Entity entity, int clientId)
    {
        if (Engine._api.Net_SetOwner != null) Engine._api.Net_SetOwner(entity, clientId);
    }
    /// <summary>Client: whether this client owns (and predicts) the entity.</summary>
    public static bool IsOwner(Entity entity)
    {
        if (!IsClient) return false;
        int id = LocalClientId;   // 0 until connected: owns nothing yet
        return id != 0 && GetOwner(entity) == id;
    }

    /// <summary>
    /// On a client: the entity it predicts (its pawn).
    /// </summary>
    public static bool IsPredicted(Entity entity) => Engine._api.Net_IsPredicted != null && Engine._api.Net_IsPredicted(entity) != 0;

    /// <summary>
    /// The owner's command for the tick being simulated (server: as received; client: its own).
    /// </summary>
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
        command.ViewTick = Engine._api.Net_GetCommandViewTick != null ? Engine._api.Net_GetCommandViewTick(entity) : -1.0;
        if (Engine._api.Net_GetCommandAim != null)
        {
            float yaw, pitch;
            command.HasAim = Engine._api.Net_GetCommandAim(entity, &yaw, &pitch) != 0;
            command.Yaw = yaw;
            command.Pitch = pitch;
        }
        return command;
    }

    /// <summary>
    /// Holds (or releases) a networked action as if its input were down: bots and tests.
    /// </summary>
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

    /// <summary>
    /// NET-15: a shot judged the way its shooter saw it. On the server the hitboxes (NetHitbox) are
    /// rewound to the view the shooter's command was sent from (Command.ViewTick, capped at the
    /// rewind window: Clamped), and present-day colliders and rewound occluders still block it. On
    /// a client (or offline) it tests the hitboxes as they are now: the predicted hit, for impact
    /// FX; only the server's result counts. Shooter: the entity firing (ignored by the ray).
    /// </summary>
    public static NetHit LagCompensatedRaycast(Entity shooter, Vector3 origin, Vector3 direction, float range)
    {
        if (Engine._api.Net_LagCompensatedRaycast == null) return default;
        Entity hit;
        Vector3 point;
        int flags;
        Engine._api.Net_LagCompensatedRaycast(shooter, &origin, &direction, range, &hit, &point, &flags);
        return new NetHit((flags & 1) != 0, hit, point, (flags & 2) != 0, (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0);
    }

    /// <summary>
    /// The local player's absolute aim (NET-15), sent with every command until cleared: degrees,
    /// yaw about +Y (0 down +Z, 90 down +X), pitch up from level. Set it from where the view looks
    /// each frame; the pawn's script reads Net.Command(Entity).Yaw / Pitch / AimDirection in
    /// OnFixedUpdate on both sides, so a predicted shot and the server's aim the same way.
    /// </summary>
    public static void SetAim(float yawDegrees, float pitchDegrees) { if (Engine._api.Net_SetAim != null) Engine._api.Net_SetAim(1, yawDegrees, pitchDegrees); }
    /// <summary>Stops sending an aim.</summary>
    public static void ClearAim() { if (Engine._api.Net_SetAim != null) Engine._api.Net_SetAim(0, 0f, 0f); }

    /// <summary>
    /// Smoothed round trip in ms: a client's to the server, or (server) to one client.
    /// </summary>
    public static float RoundTripMs => Engine._api.Net_GetRoundTripMs != null ? Engine._api.Net_GetRoundTripMs(0) : 0f;
    /// <summary>Server: the round trip to one client, in ms.</summary>
    public static float RoundTripMsTo(int clientId) => Engine._api.Net_GetRoundTripMs != null ? Engine._api.Net_GetRoundTripMs(clientId) : 0f;

    /// <summary>
    /// Whether to draw impacts (sparks, decals, hit sounds) from a client's own predicted hit. Above
    /// PredictImpactsMaxRttMs the predicted view is too far from the server's judgement: wait for
    /// its confirmation (an RPC, a replicated field) instead. Hit markers and health always should.
    /// </summary>
    public static float PredictImpactsMaxRttMs = 220f;
    /// <summary>Whether to draw impacts from this peer's predicted hits (false on a client above PredictImpactsMaxRttMs: wait for the server).</summary>
    public static bool PredictImpacts => !IsClient || RoundTripMs <= PredictImpactsMaxRttMs;

    /// <summary>
    /// Prediction on this client: mispredictions corrected, ticks that matched, and initial syncs.
    /// </summary>
    public static int Corrections { get { GetPredictionStats(out int c, out _, out _); return c; } }
    /// <summary>Predicted ticks that matched the server.</summary>
    public static int Matches { get { GetPredictionStats(out _, out int m, out _); return m; } }
    /// <summary>Times prediction took the server's state without a correction (joining, ownership changes).</summary>
    public static int InitialSyncs { get { GetPredictionStats(out _, out _, out int s); return s; } }

    private static void GetPredictionStats(out int corrections, out int matches, out int initialSyncs)
    {
        int c = 0, m = 0, s = 0;
        if (Engine._api.Net_GetPredictionStats != null) Engine._api.Net_GetPredictionStats(&c, &m, &s);
        corrections = c;
        matches = m;
        initialSyncs = s;
    }

    /// <summary>
    /// The id an entity is known by on every peer (0: not networked), and back.
    /// </summary>
    public static int GetNetId(Entity entity) => Engine._api.Net_GetNetId != null ? Engine._api.Net_GetNetId(entity) : 0;
    /// <summary>The entity a NetId names here, or Entity.Null.</summary>
    public static Entity FindEntity(int netId)
    {
        if (netId == 0 || Engine._api.Net_FindEntity == null) return Entity.Null;
        Entity id = Entity.Null;
        Engine._api.Net_FindEntity(netId, &id);
        return id;
    }

    /// <summary>
    /// An RPC's arguments must fit one message (1024 bytes) with its header.
    /// </summary>
    public const int MaxRpcArgsBytes = 1008;

    /// <summary>
    /// Generated RPC stubs call this. kind: 0 to the server, 1 to the owner, 2 to every client.
    /// Offline both sides are local, and an RPC addressed to the side we're on runs here directly.
    /// </summary>
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

    /// <summary>ME-291: a cosmetic event (FX, a sound, a log line) raised from simulation (OnFixedUpdate) that must show once, not in every resimulation of its tick. Offline: script.OnNetEvent(eventId, value) now. The server: now, and to every client. The predicting owner: now, once per (entity, tick, event); resims and the server's copy skip it. Other clients: when the server's copy arrives. Needs a networked script on a NetIdentity.</summary>
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

    /// <summary>
    /// The RPC id PredictedEvent's server copies travel under (C++: kPredictedEventRpcId).
    /// </summary>
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

    /// <summary>
    /// Generated RPC stubs with string arguments: the arguments didn't fit the RPC buffer.
    /// </summary>
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


/// <summary>
/// ME-291: the predicted events a client has presented, by (entity, tick, event), so a resimulated
/// tick or the server's copy doesn't present one twice. Keeps about the last 256 ticks.
/// </summary>
public sealed class NetEventLedger
{
    private readonly HashSet<(uint Index, uint Generation, int Tick, int EventId)> _presented = new();
    private int _newestTick = int.MinValue;

    /// <summary>How many events it remembers.</summary>
    public int Count => _presented.Count;

    /// <summary>
    /// True the first time (present it now), false for a repeat.
    /// </summary>
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

    /// <summary>Forgets every event.</summary>
    public void Clear()
    {
        _presented.Clear();
        _newestTick = int.MinValue;
    }
}
