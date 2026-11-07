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

    /// <summary>A networked Vector2 action's two axes (Move): zero when it isn't in the command layout.</summary>
    public Vector2 Vector2(string action)
    {
        int index = Net.AxisIndex(action, out int count);
        return index < 0 ? default : new Vector2(Axis(index), count > 1 ? Axis(index + 1) : 0f);
    }

    /// <summary>A networked Value action's axis: zero when it isn't in the command layout.</summary>
    public float Value(string action)
    {
        int index = Net.AxisIndex(action);
        return index < 0 ? 0f : Axis(index);
    }

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
/// EventBus, on the server: a client was accepted into the session (its Welcome is on the way).
/// Spawn what it plays with and hand it over with Net.SetOwner.
/// </summary>
/// <param name="ClientId">The client's id (1-255).</param>
public readonly record struct NetClientJoined(int ClientId);
/// <summary>
/// EventBus, on the server: a client left the session. Its entities stay (owned by the server
/// again): destroy what was its.
/// </summary>
/// <param name="ClientId">The client's id (free for a later client).</param>
/// <param name="Reason">"left" (it said goodbye) or "timed out". Not raised when this server shuts down: scripts
/// have stopped by then.</param>
public readonly record struct NetClientLeft(int ClientId, string Reason);
/// <summary>
/// EventBus, on a client: the server welcomed this client (Net.LocalClientId is set from now on).
/// </summary>
/// <param name="ClientId">This client's id (1-255).</param>
public readonly record struct NetConnected(int ClientId);
/// <summary>
/// EventBus, on a client: the session ended, or never started.
/// </summary>
/// <param name="Reason">What the server said when it closed ("server closed", "timed out"), "timed out" (the
/// server went quiet), or "rejected: " and the server's reason (anything that isn't text comes through as
/// U+FFFD). Not raised when this client quits: scripts have stopped by then.</param>
public readonly record struct NetDisconnected(string Reason);
/// <summary>
/// EventBus, on a client: a killcam started playing (ME-323; also Net.Killcam.Started). The screen shows
/// its replay world from now until KillcamEnded.
/// </summary>
/// <param name="Id">The killcam's id (Net.Killcam.Id).</param>
/// <param name="KillerId">The killer's NetId.</param>
/// <param name="VictimId">The victim's NetId (this player's pawn).</param>
public readonly record struct KillcamStarted(int Id, int KillerId, int VictimId);
/// <summary>
/// EventBus, on a client: a killcam ended (ME-323; also Net.Killcam.Ended); the screen shows the live world again.
/// </summary>
/// <param name="Id">The killcam's id.</param>
/// <param name="KillerId">The killer's NetId.</param>
/// <param name="VictimId">The victim's NetId.</param>
/// <param name="Reason">"played" (to the death and a second on it), "skipped" (Net.Killcam.Skip), "replaced" (a newer
/// one started), "stopped" (the session ended or a scene loaded) or "failed: " and why.</param>
public readonly record struct KillcamEnded(int Id, int KillerId, int VictimId, string Reason);


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
    /// The local player's command for this fixed step: what a pawn simulates with when there's no
    /// session (offline play, the editor's Play) through the same code as online. Offline it's
    /// sampled every fixed step from the networked input actions (with Net.Press and the aim from
    /// Net.SetAim), axes as the server would read them, Tick counting the steps; Valid while any
    /// action is networked (or Press / SetAim holds something), and WasPressed compares with the
    /// step before. The editor's Stop releases Press holds and the aim. On a client: its
    /// predicted pawn's command (a replayed tick's while resimulating), or before it has one, the
    /// command it sends. On the server: Valid is false (it has no local player).
    /// </summary>
    public static NetCommand LocalCommand
    {
        get
        {
            var command = new NetCommand { ViewTick = -1.0 };
            if (Engine._api.Net_GetLocalCommand == null) return command;
            int buttons, previous, tick;
            double viewTick;
            float* axes = stackalloc float[4];
            float* aim = stackalloc float[2];
            int flags = Engine._api.Net_GetLocalCommand(&buttons, &previous, axes, aim, &viewTick, &tick);
            command.Valid = (flags & 1) != 0;
            if (!command.Valid) return command;
            command.Tick = tick;
            command.Buttons = buttons;
            command.PreviousButtons = previous;
            command.Axis0 = axes[0];
            command.Axis1 = axes[1];
            command.Axis2 = axes[2];
            command.Axis3 = axes[3];
            command.ViewTick = viewTick;
            command.HasAim = (flags & 2) != 0;
            command.Yaw = aim[0];
            command.Pitch = aim[1];
            return command;
        }
    }

    /// <summary>
    /// A client's predicted pawn: where it's drawn relative to its simulated pose (Transform), the
    /// rest of a correction being smoothed out (gone in about 100 ms). The engine draws the pawn and
    /// everything under it (meshes, an attached camera, audio sources) there itself, after the late
    /// update; scripts always read the simulated pose. Add this to place something from the pawn's
    /// pose yourself (a camera that isn't its child, an effect). Corrections over 2 m (a teleport, a
    /// respawn) snap, so it's zero then. Zero for anything else, on the server and offline.
    /// </summary>
    public static Vector3 VisualOffset(Entity pawn)
    {
        Vector3 offset = default;
        if (Engine._api.Net_GetVisualOffset != null) Engine._api.Net_GetVisualOffset(pawn, &offset);
        return offset;
    }

    /// <summary>
    /// A networked Button action's bit in NetCommand.Buttons (-1 when it isn't one): build commands
    /// (bots, tests) with Buttons |= 1 &lt;&lt; Net.ButtonIndex("Fire"). The layout is the same on every peer.
    /// </summary>
    public static int ButtonIndex(string action) => ActionIndex(action);

    /// <summary>
    /// A networked Value / Vector2 action's first axis in the command (Axis0..Axis3; a Vector2 takes
    /// two in a row), or -1 when it isn't one. The layout is the same on every peer.
    /// </summary>
    public static int AxisIndex(string action) => AxisIndex(action, out _);

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
        => LagCompensatedRaycast(shooter, origin, direction, range, Entity.Null);

    /// <summary>
    /// The same, for a projectile sweeping its flight: shooter is the projectile (ignored by the ray),
    /// instigator who fired it. The match replay (ME-316) attributes the shot to the instigator and its
    /// owner, and counts it as a projectile sweep rather than a trigger pull.
    /// </summary>
    public static NetHit LagCompensatedRaycast(Entity shooter, Vector3 origin, Vector3 direction, float range, Entity instigator)
    {
        if (Engine._api.Net_LagCompensatedRaycast == null) return default;
        Entity hit;
        Vector3 point;
        int flags;
        Engine._api.Net_LagCompensatedRaycast(shooter, instigator, &origin, &direction, range, &hit, &point, &flags);
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
    /// Server: sends the victim's owner a killcam (ME-323), the last <paramref name="seconds"/> before this
    /// tick as the killer saw them: what the killer's client was in view of, its aim and commands, its
    /// shots and the replay events (Replay.Event) about what's in view, cut from the server's replay ring
    /// and streamed over the blob channel. The game decides what a kill is. False off the server,
    /// without the ring (a server keeps it unless started with --no-replay-ring), for a victim no client
    /// owns (a bot) or a killer that isn't networked. A newer one for the same player replaces the older.
    /// </summary>
    public static bool SendKillcam(Entity victim, Entity killer, float seconds = 8f) =>
        Engine._api.Net_SendKillcam != null && Engine._api.Net_SendKillcam(victim, killer, seconds) != 0;

    /// <summary>
    /// Client: the newest killcam the server sent (ME-323). It arrives a chunk at a time; a newer one
    /// replaces it. The Player plays each one as soon as its first chunk is in, into a replay world of its
    /// own beside the live one (which keeps its session, prediction and simulation): the screen, the
    /// camera and the audio listener show the killer's eye along its recorded aim until the death tick
    /// has held a second, then switch straight back. Started and Ended say when; Skip ends one early. The
    /// game's UI stays over it. Scripts marked [ReplayWorld] run in the replay world too, presentation
    /// only: InReplayWorld tells them which world they're in.
    /// </summary>
    public static class Killcam
    {
        /// <summary>A killcam is playing (the screen shows its replay world).</summary>
        public static bool IsPlaying => (Playback(out _) & 1) != 0;
        /// <summary>This script runs in a killcam's replay world ([ReplayWorld] scripts): present, don't touch the game's state.</summary>
        public static bool InReplayWorld => (Playback(out _) & 2) != 0;
        /// <summary>The id of the killcam playing, or of the last one played (0: none yet); Id is the newest received, which can be a newer one than this.</summary>
        public static int PlayingId { get { Playback(out var p); return p.Id; } }
        /// <summary>The killer's NetId (Net.GetNetId) in the killcam playing, or the last one played (0: none yet).</summary>
        public static int KillerId { get { Playback(out var p); return p.KillerId; } }
        /// <summary>The victim's NetId in the killcam playing, or the last one played (0: none yet).</summary>
        public static int VictimId { get { Playback(out var p); return p.VictimId; } }
        /// <summary>The tick the killcam shows (fractional between ticks), from FirstTick to DeathTick.</summary>
        public static float Tick { get { Playback(out var p); return p.Tick; } }
        /// <summary>The killcam's first tick.</summary>
        public static int FirstTick { get { Playback(out var p); return p.FirstTick; } }
        /// <summary>The death's tick, the killcam's last.</summary>
        public static int DeathTick { get { Playback(out var p); return p.DeathTick; } }
        /// <summary>Seconds it has played.</summary>
        public static float Elapsed { get { Playback(out var p); return p.Elapsed; } }
        /// <summary>Seconds it plays: its ticks at the tick rate, then a second on the death.</summary>
        public static float Duration { get { Playback(out var p); return p.Duration; } }
        /// <summary>Ends the killcam playing: the screen switches back to the live world (this frame from a live world's script, the next from the replay world's).</summary>
        public static void Skip()
        {
            if (Engine._api.Net_SkipKillcam != null) Engine._api.Net_SkipKillcam();
        }
        /// <summary>Where the game puts its eye above a pawn's origin, in metres: the killer's eye in its killcams (ME-343).
        /// Set it from the live world (a pawn's OnStart); below 0 (the default): the engine's guess, the CharacterController's top less 0.15 m.</summary>
        public static float EyeHeight
        {
            get => Engine._api.Net_GetKillcamEyeHeight != null ? Engine._api.Net_GetKillcamEyeHeight() : -1f;
            set { if (Engine._api.Net_SetKillcamEyeHeight != null) Engine._api.Net_SetKillcamEyeHeight(value); }
        }

        /// <summary>A killcam started playing (EventBus KillcamStarted; a script's handler goes with it).</summary>
        public static event Action<KillcamStarted> Started
        {
            add => EventBus.Subscribe(value);
            remove => EventBus.Unsubscribe(value);
        }
        /// <summary>A killcam ended: played, skipped, replaced, stopped or failed (EventBus KillcamEnded; a script's handler goes with it).</summary>
        public static event Action<KillcamEnded> Ended
        {
            add => EventBus.Subscribe(value);
            remove => EventBus.Unsubscribe(value);
        }

        private struct PlaybackState
        {
            public int Id, KillerId, VictimId, FirstTick, DeathTick;
            public float Tick, Elapsed, Duration;
        }

        private static int Playback(out PlaybackState state)
        {
            state = default;
            if (Engine._api.Net_GetKillcamPlayback == null) return 0;
            int id = 0, killer = 0, victim = 0, first = 0, death = 0;
            float tick = 0f, elapsed = 0f, duration = 0f;
            int flags = Engine._api.Net_GetKillcamPlayback(&id, &killer, &victim, &tick, &first, &death, &elapsed, &duration);
            state = new PlaybackState { Id = id, KillerId = killer, VictimId = victim, FirstTick = first, DeathTick = death, Tick = tick, Elapsed = elapsed, Duration = duration };
            return flags;
        }

        /// <summary>The newest one's id (from 1; 0: none yet). A new id is a new killcam.</summary>
        public static int Id { get { Get(out int id, out _, out _, out _); return id; } }
        /// <summary>Whether the newest one is here whole.</summary>
        public static bool Received => Get(out _, out _, out _, out _);
        /// <summary>How much of the newest one is here, 0..1 (1 once received).</summary>
        public static float Progress { get { Get(out _, out float progress, out _, out _); return progress; } }
        /// <summary>The killer in this world (Entity.Null when it isn't here any more).</summary>
        public static Entity Killer { get { Get(out _, out _, out Entity killer, out _); return killer; } }
        /// <summary>The victim in this world (Entity.Null when it isn't here any more).</summary>
        public static Entity Victim { get { Get(out _, out _, out _, out Entity victim); return victim; } }

        private static bool Get(out int id, out float progress, out Entity killer, out Entity victim)
        {
            int i = 0;
            float p = 0f;
            Entity k = Entity.Null, v = Entity.Null;
            bool complete = Engine._api.Net_GetKillcam != null && Engine._api.Net_GetKillcam(&i, &p, &k, &v) != 0;
            id = i;
            progress = p;
            killer = k;
            victim = v;
            return complete;
        }
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
    /// ME-343: the id of the script world's net session (unique in the process: a new one for each host, connect, and
    /// replay or killcam played; 0 when there's none). Net.Events keeps a ledger per session.
    /// </summary>
    public static int SessionId => Engine._api.Net_GetSessionId != null ? Engine._api.Net_GetSessionId() : 0;

    /// <summary>
    /// The RPC id PredictedEvent's server copies travel under (C++: kPredictedEventRpcId).
    /// </summary>
    public const int PredictedEventRpcId = 0x3FFFFFFF;

    // ME-291's ledger of the predicted events presented, one per session (ME-343): each world's (a killcam's replay
    // world beside the live one) and each killcam played into a kept replay world has its own, so ticks and entity
    // ids that repeat across them never collide. The few most recent are kept.
    internal static readonly NetEventLedgers EventLedgers = new();
    internal static NetEventLedger Events => EventLedgers.For(SessionId);

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

    // Action name -> networked button bit, -1 when it isn't one, and -> first axis and axis count.
    // Cached (only a miss allocates); looked up again when the role changes (the session owns the
    // button map) or the layout is rebuilt (offline, after the action map reloads).
    private static readonly Dictionary<string, int> _actionIndices = new();
    private static readonly Dictionary<string, (int Index, int Count)> _axisIndices = new();
    private static int _actionIndicesRole = -1;
    private static int _actionIndicesLayout = -1;
    private static readonly HashSet<string> _warned = new();

    private static void CheckLayout()
    {
        int role = Role;
        int layout = Engine._api.Net_GetLayoutVersion != null ? Engine._api.Net_GetLayoutVersion() : 0;
        if (role != _actionIndicesRole || layout != _actionIndicesLayout)
        {
            _actionIndices.Clear();
            _axisIndices.Clear();
            _actionIndicesRole = role;
            _actionIndicesLayout = layout;
        }
    }

    internal static int ActionIndex(string action)
    {
        if (action is null) return -1;
        CheckLayout();
        if (_actionIndices.TryGetValue(action, out int index)) return index;
        if (Engine._api.Net_GetActionIndex == null) return -1;
        fixed (byte* p = Engine.Utf8(action, stackalloc byte[Engine.Utf8StackSize])) index = Engine._api.Net_GetActionIndex(p);
        _actionIndices[action] = index;
        return index;
    }

    internal static int AxisIndex(string action, out int count)
    {
        count = 0;
        if (action is null) return -1;
        CheckLayout();
        if (_axisIndices.TryGetValue(action, out var known))
        {
            count = known.Count;
            return known.Index;
        }
        if (Engine._api.Net_GetAxisIndex == null) return -1;
        int index, axes;
        fixed (byte* p = Engine.Utf8(action, stackalloc byte[Engine.Utf8StackSize])) index = Engine._api.Net_GetAxisIndex(p, &axes);
        _axisIndices[action] = (index, axes);
        count = axes;
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
        EventLedgers.Clear();
        _actionIndices.Clear();
        _axisIndices.Clear();
        _actionIndicesRole = -1;
        _actionIndicesLayout = -1;
        _warned.Clear();
    }
}


/// <summary>
/// ME-343: NetEventLedgers by net session (Net.SessionId): the session asked for most recently first; past
/// MaxSessions the one asked for least recently goes (a session that ended, a killcam played out).
/// </summary>
public sealed class NetEventLedgers
{
    /// <summary>How many sessions' ledgers are kept.</summary>
    public const int MaxSessions = 4;

    private readonly List<(int Session, NetEventLedger Ledger)> _ledgers = new();

    /// <summary>How many sessions have a ledger.</summary>
    public int Count => _ledgers.Count;

    /// <summary>The session's ledger (made on first use).</summary>
    public NetEventLedger For(int session)
    {
        for (int i = 0; i < _ledgers.Count; ++i)
        {
            if (_ledgers[i].Session != session) continue;
            var found = _ledgers[i];
            if (i != 0)
            {
                _ledgers.RemoveAt(i);
                _ledgers.Insert(0, found);
            }
            return found.Ledger;
        }
        var ledger = new NetEventLedger();
        _ledgers.Insert(0, (session, ledger));
        if (_ledgers.Count > MaxSessions) _ledgers.RemoveAt(_ledgers.Count - 1);
        return ledger;
    }

    /// <summary>Forgets every session's.</summary>
    public void Clear() => _ledgers.Clear();
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
