# Networking

Multiplayer follows the Overwatch model: an authoritative server, client prediction, and C# first. It is built in steps, NET-0 to NET-11 on the roadmap. What exists so far is the foundation:

- **NET-0:** a deterministic character step and its replayable state (see [Physics.md](Physics.md)). Scripts can act on a scoped world (see [Scripting-DotNet.md](Scripting-DotNet.md)).
- **NET-1:** the wire layer: the bit stream, quantization, transports, and a connection with acks, round trip, loss and a reliable ordered stream. It's pure C++ with no engine dependencies beyond the math types.
- **NET-2:** sessions:
  - the handshake, with a version check;
  - command frames, sent redundantly into the server's per-client buffers;
  - the client clock, with dilation;
  - the engine-owned `NetCore` in the frame;
  - a two-world test harness.

- **NET-3:** replication of `NetIdentity` entities:
  - static and dynamic NetIds;
  - reliable spawns and despawns;
  - delta snapshots built with `NetSchema` from reflection;
  - client interpolation;
  - kinematic remotes.

- **NET-4:** client prediction of the owner's character, with rollback, resim and error smoothing.
- **NET-8:** lag compensation: `NetHitbox`, a rewound `LagCompensatedRaycast`, and its overlays.
- **NET-13:** relevancy, a priority accumulator, a per-client bandwidth budget, and snapshot encoding on jobs.
- **NET-5:** real networking:
  - `UdpTransport`, plain UDP over IPv4 (no ENet: `NetConnection` already does the reliability);
  - session timeouts and a graceful Disconnect (protocol 2);
  - a headless **Server** build (`config=server_linux_debug`: no window, bgfx Noop, no ImGui);
  - `--server` / `--connect` / `--net-*` on the command line, with a JSON report;
  - `Tools/RunNetSmoke.py` in RunChecks.

- **NET-6:** networking from C#:
  - `[Replicated]` / `[Predicted]` script fields and `[ServerRpc]` / `[ClientRpc]` methods, generated allocation-free by the NetGenerator;
  - the `Net` API: role, commands, ownership, prediction stats;
  - networked input actions;
  - script prediction with rollback and resim;
  - ownership that reaches clients;
  - the schema in the handshake.

The editor tooling (NET-7) comes next.

## Key Files

| Path | Role |
|------|------|
| `Source/Net/BitStream.h` | `BitWriter` / `BitReader`: bits LSB-first into little-endian bytes, VarUInt, raw floats, bytes, strings. Readers never overrun: they return zero and set `IsOverflowed()` |
| `Source/Net/NetQuantize.h` | A float in a range at N bits, round-tripping within half a step (out of range clamps, NaN maps to the minimum). `Vector3` per component; quaternions as "smallest three", 2 + 3·N bits |
| `Source/Net/NetTransport.h` | `INetTransport` (datagrams between `NetAddress`es). `LoopbackNetwork` + `LoopbackTransport` join worlds in one process. `ConditionedTransport` wraps any transport with `NetConditions` |
| `Source/Net/NetConnection.h` | `NetConnection`, one end of a link, and `NetStats` |
| `Source/Net/NetVersion.h` | What both sides must agree on: protocol, build (product name and version), C# script classes, mod set, placed entities (sorted GUIDs). FNV-1a hashes, the same on every platform |
| `Source/Net/NetCommands.h` | `InputCommand` (tick, buttons, 4 axes at 16 bits), delta-coded lists of up to 16, and `NetCommandBuffer`, the server's per-client queue |
| `Source/Net/NetClock.h` | The client's command-frame clock: target slack, dilation up to ±5%, a resync beyond 8 ticks |
| `Source/Net/NetSession.h` | `NetServer` and `NetClient`: the handshake, commands up, and server reports down |
| `Source/Components/Networking/NetIdentity.h` | Marks an entity networked: NetId, owner, the prefab clients spawn |
| `Source/Net/NetSnapshot.h` | `NetEntityState`: pose at 1/1024 m, rotation smallest-three at 12 bits, replicated field bytes. `NetSchema` (fields flagged `.Replicated()` in `ME_REFLECT`). `NetSnapshotCodec`: deltas against a baseline |
| `Source/Net/NetReplication.h` | `NetReplicationServer` (ids, spawns, snapshots per client) and `NetReplicationClient` (mirrors, interpolation) |
| `Source/Net/NetPrediction.h` | `NetPredictor`: a 128-tick history of {command, mover state}, `Reconcile` (millimetre compare, then restore and resim with `PhysicsCore::StepCharacter`), and visual error smoothing |
| `Source/Components/Networking/NetHitbox.h` | A capsule or box that lag-compensated shots test against |
| `Source/Net/NetLagCompensation.h` | Each tick's hitbox poses for `MaxRewindSeconds`; `Raycast` at a view tick (analytic ray vs capsule / OBB, present colliders occlude); overlays |
| `Source/Cores/NetCore.h` | Engine-owned (`Engine::Net`): `Host` / `Connect` / `Close`, the frame hooks (they drive the transport's `Update`), the version of its world, `WriteReport` |
| `Source/Net/UdpTransport.h` | `UdpTransport`: non-blocking IPv4 UDP (BSD sockets / Winsock). Addresses pack `(ip << 16) \| port`; `Resolve( "host:port" )`, `ToString` |
| `Source/Engine/NetLaunch.h` | The command line (`--server`, `--connect`, `--net-*`): starts the session after the scene loads, logs it, writes the report, says goodbye on exit |
| `Source/Window/NullWindow.h` | The headless build's window: a size and SDL's event loop (Ctrl+C / SIGTERM close it) |
| `Tools/RunNetSmoke.py` | The two-process smoke test (below) |
| `Tools/RunNetLocal.py` | A server plus N client windows on this machine, for playing and debugging (below) |
| `Modules/ScriptCore/Source/Net/` | C#: the `[Replicated]` / `[Predicted]` / `[ServerRpc]` / `[ClientRpc]` attributes, `INetScript`, `NetWriter` / `NetReader`, `Net`, and `NetBridge.cs` (the entry points C++ calls) |
| `Modules/ScriptCore/Generators/NetGenerator.cs` | The NET-6 generator |
| `Source/Scripting/ScriptNet.h`, `Bindings/Systems/Net.bindings.cpp` | `NetCore::ScriptGlue` over C# scripts, and the `Net_*` slots C# calls |
| `Tests/Source/NetTests.cpp`, `NetSessionTests.cpp`, `NetHarness.h` | The proofs (below). The harness runs a server world and a client world in one process at 1 ms resolution |

## How It Works

### Conditioned links

`NetConditions` sets one-way latency, ± jitter, loss, duplicates, and a reorder chance with its extra delay. Draws come from a seeded xorshift, so a seed gives the same link on every platform.

- **Outgoing only:** conditions apply to what that end sends, so wrap both ends for a two-way link.
- **Update every tick:** `Update( now )` releases packets as they come due.
- **Delivery resolution:** a test harness should deliver at about 1 ms resolution, not once a tick. Otherwise arrival rounds up to the tick and reads as extra latency.

### NetConnection

Each `SendPacket( now )` sends exactly one packet. It carries:

- **Header:** protocol id `0x4D45`, a 16-bit sequence, and the latest sequence received plus 32 bits for the ones before it, so each packet acks 33. It also carries the **ack delay**: how long that latest packet waited before this ack, in 0.1 ms units.
- **Reliable messages:** new ones, plus any unacked for a little over a round trip (`1.25 × RTT + 2 × deviation`, at least 20 ms; 100 ms before the first estimate). They go oldest first, within a 1024-message window and the 1200-byte packet budget.
- **Unreliable messages:** the ones queued since the last packet. Each goes in this packet or is dropped.

On receive, the connection works through these steps:

1. **Validate:** a packet with the wrong protocol id or a malformed body is rejected whole, before anything is applied.
2. **Duplicates:** a sequence it has seen, or one older than the ack window, is dropped as a duplicate.
3. **Acks:** these settle the sent-packet ring. Each settled packet's reliable messages stop resending.
4. **Round trip:** taken from the newest acked packet, minus the ack delay, then smoothed (1/8 gain, deviation 1/4).
5. **Loss:** a packet that leaves the 33-packet window unacked counts as lost (`PacketsLost`, a smoothed `PacketLoss`).
6. **Reliable delivery:** messages are delivered in id order exactly once. Early ones wait for the gap to fill.

Messages are at most 1024 bytes (`kMaxMessageBytes`); there is no fragmentation yet. Sequences and message ids wrap at 16 bits (`NetSequenceGreater`).

### Sessions (NET-2)

1. **Hello:** the client sends Hello with its `NetVersion` on the reliable stream. A server whose version differs answers **Reject** with what differs (`"version mismatch: build scene"`). Otherwise it answers **Welcome**: client id, tick rate, its tick. A rejected client is dropped once the Reject is acked, or after 2 s; a connection that never says hello is dropped after 10 s.
2. **Start tick:** on Welcome, the client starts its clock at `server tick + RTT in ticks + target slack`. The server is half a round trip past that tick by then; the client runs half a round trip further ahead, plus the slack.
3. **Commands up:** each client tick stamps the local command (`NetCore::SetLocalCommand`, later the HK-5 sampler) with the clock's tick. Each client packet carries the newest unconfirmed commands, up to 16, delta-coded, so a lost packet's commands ride the next ones.
4. **Server ticks:** each tick, the server takes every client's command for that tick from its `NetCommandBuffer`. A missing command repeats the previous one (a miss), and the server never stalls.
5. **Slack:** measured at each consume as `newest buffered tick − tick`, then smoothed (1/10, or taken at once after a jump of more than 4).
6. **Reports down:** each server packet reports to each client the tick, the newest command it holds, and that client's slack.
7. **Clock control:** the client drops confirmed commands and feeds the slack to `NetClock`, which sets dilation to `−(slack − target) × 0.02`, clamped to ±5%. More than 8 ticks off, it jumps instead, then ignores reports for 30 ticks (they describe the old clock).

**Frame hooks** (`Engine::Run`; all no-ops without a session):

| Hook | Where | Effect |
|------|-------|--------|
| `Net->Receive( now )` | After the frame's events | Packets in |
| `Net->BeginTick()` | Start of each fixed step | The server takes commands; the client stamps its command |
| `Net->EndTick( now )` | After the physics step | The server reports, one packet per client |
| `Net->Flush( now )` | Before the frame limiter | The client's packet: at most about one a tick (each new command sends; otherwise a keep-alive after two ticks without one), however fast it draws |
| `GetClockScale()` | The fixed-step accumulator | Scaled by it: dilation without changing `FixedDeltaTime` |
| `KeepsBacklog()` | Backlog drop | A server catches up instead of dropping steps |

**Defaults:** the target slack is 3 ticks. That covers ±10 ms of jitter plus two lost packets in a row at 60 Hz.

**Ending (NET-5, protocol 2):**

- **Disconnect:** a leaving client (`NetClient::Disconnect`, `NetCore::Close`) or a closing server (`NetServer::Close`) sends **Disconnect** with a reason. It is unreliable, since the sender is going, so it goes in three packets of its own. The server drops that client on its next `Receive` and counts it (`GetDisconnectCount`); the client becomes `Disconnected`, and `GetDisconnectReason()` says why (`"left"`, `"server closed"`).
- **Timeouts:** a peer that sends nothing for the timeout (10 s, `SetTimeout`, `--net-timeout`) is gone.
  - The server drops a silent accepted client and counts it (`GetTimeoutCount`). It still says Disconnect (`"timed out"`) on that client's own connection, in case the client is alive after all.
  - The server only sends to addresses that said hello. Packets from a session it has dropped get no answer, so such a client times out instead of hanging on half-open.
  - A client stops at `State::TimedOut`, counted from its Hello first going out, including while still connecting to a server that never answers.
- **One session per `NetClient`:** `Connect` works once; reconnecting needs a new client. Client ids (1–255) are reused only once no accepted client holds them.
- **`OnClientRemoved`:** fires for either, before the client goes. Replication forgets that client's state there, and `NetCore` keeps a summary for its report.

### Replication (NET-3)

**Ids:**

- **Static:** placed `NetIdentity` entities get NetIds 1..N on both sides, in GUID order. The handshake's scene hash covers exactly these entities, so the orders match.
- **Dynamic:** entities the server gives a `NetIdentity` during the session get ids from `0x10000`. A reliable **Spawn** `{NetId, owner, prefab, pose}` goes to every client; the client creates it from `Prefab`, or as a bare entity with a Transform.
- **Despawn:** destroying a networked entity sends a reliable Despawn.
- **Late joiners:** they get Spawns for every dynamic entity, and Despawns for the static ones already destroyed.

**Snapshots:**

1. **Capture:** after each server tick, `CaptureTick` records every networked entity's `NetEntityState` (quantized world pose plus `NetSchema` bytes).
2. **Delta:** each client's packet carries a delta against the newest snapshot that client acked (`SnapshotAck`, sent every client packet), or the full state with no baseline. The server keeps the last 32 sent per client. Changed entities write only the parts that changed, gone ones are listed, and the 900-byte budget defers the rest; the "sent" copy records what the client will really have.
3. **Skip when idle:** a snapshot identical to the last one sent is skipped, with a keep-alive every 15 ticks. The next snapshot names the previous one sent, so a client that has it knows entities held still in between (it adds a hold sample) rather than gliding from an old pose.

**Clients:**

- **Baselines:** decoded snapshots are kept (the last 32) as delta baselines.
- **Samples:** each entity gets a sample buffer.
- **Render tick:** `WriteInterpolated` (after physics poses) shows each entity at render tick = `newest server tick − delay`. It's steered gently and snaps when more than 6 ticks off.
- **Interpolation:** linear for position, slerp for rotation, holding past the last sample. Replicated fields apply from the sample at or before the render tick.
- **Adaptive delay:** it grows by 0.25 tick when a moving entity runs out of samples (up to 8), and shrinks slowly after 2 s without that (down to 1.5).
- **Kinematic remotes:** a client's copy of a networked `Rigidbody` is made kinematic; only the server simulates.

**`NetSchema` field types:** bool, ints and enums (VarUInt), float, double, Vector2/3/4, Quaternion (raw floats), and strings. Flag fields with `ME_FIELD( X ).Replicated()`. `.Predicted()` implies Replicated. Script fields ride separately, after the reflected ones (NET-6, below).

### Relevancy and bandwidth (NET-13)

`NetReplicationServer::Settings` (`NetReplicationSettings`) shares out each client's bandwidth.

**Every tick, for every client:**

1. **Viewer:** `SetViewer( client, position )`, else the first entity the client owns.
2. **View:** entities within `RelevantRadius` of the viewer, plus `NetIdentity::AlwaysRelevant` ones and the client's own. An entity that leaves the view is listed as gone in that client's snapshots and holds its last pose there; when it comes back, it's sent whole.
3. **Accumulate:** each changed entity in the view adds to its accumulator: `Priority × (1 within NearDistance, else NearDistance / distance)`.
4. **Budget:** the packet's 900 bytes, and with `BytesPerSecond`, a token bucket of `BytesPerSecond / tick rate` a tick, holding up to two ticks' worth.
5. **Select:** highest accumulator first, each sized from what changed, while it fits.
6. **Encode:** the snapshot writes the selected entities. Their accumulators reset; the rest keep growing, so nothing starves.
7. **Jobs:** each client's snapshot is encoded on the job system (`UseJobs`); the packets go out on the main thread. Encoding reads only the shared capture and the client's own state, so the bytes match a serial encode.

### Prediction (NET-4)

The client predicts only what it owns. Remote entities are interpolated (above), and rigidbodies simulate only on the server.

- **Command handler:** `NetCore::SetCommandHandler( fn( Entity&, InputCommand ) )` is the same function on both sides. On the server it runs at `BeginTick` for every entity a client owns (`NetIdentity::SetOwner`), with that client's command. On the client it runs for the predicted pawn (`NetCore::Predict( pawn )`), with the local command quantized exactly as the server receives it.
- **Recording:** after each client tick, `EndTick` records `{command, CharacterController::NetState}`.
- **OwnerState:** each server packet carries, for each character the client owns, its full-precision mover state after the tick.
- **Reconcile:** compares that state with the prediction for the same tick: position within 1 mm, velocity within 1 cm/s, grounded equal.
  - **On a mismatch:** restore the server's state, then for every later recorded tick apply its command and run `PhysicsCore::StepCharacter`. Only the pawn re-runs; the rest of the world stays in the present.
  - **The visual jump** (where it was drawn minus where it now is) becomes `GetVisualOffset()`, which shrinks linearly to zero over 100 ms.
  - **Before the first match**, a mismatch is the initial sync, not a correction: the server simulated idle ticks before the client's first command arrived.
- **Interpolation:** the predicted pawn is excluded from snapshot interpolation.

**Not covered yet:**
- The visual offset isn't applied to anything. A game draws the pawn's mesh at `Transform + GetVisualOffset()`. Moving the Transform itself would read as a teleport to the mover.
- Only the pawn's mover and its C# `[Predicted]` fields roll back (NET-6). Other components on it don't.

### C# networking (NET-6)

A networked script is a `partial` class deriving from `Script`:

```csharp
public partial class Dasher : Script
{
    [Predicted] public float DashCooldown;     // the owner predicts it; rolled back and resimulated
    [Replicated] public int Health;            // server -> clients, in snapshots
    [ServerRpc] partial void RequestUse(Entity door);           // client (owner) -> server: OnRequestUse runs there
    [ClientRpc(NetTarget.Owner)] partial void HitMarker(Vector3 at);  // server -> owner (NetTarget.All: every client)

    void OnRequestUse(Entity door) { /* server */ }
    void OnHitMarker(Vector3 at) { /* client */ }

    public override void OnFixedUpdate(float dt)        // simulation: server, owner, and resim
    {
        var command = Net.Command(Entity);
        if (command.WasPressed("Dash") && DashCooldown <= 0) { DashCooldown = 2; /* ... */ }
    }
}
```

**The generator** (`Modules/ScriptCore/Generators/NetGenerator.cs`, beside CS-9's `SchemaGenerator`) gives the class `INetScript`:

- **State:** fixed-size, allocation-free `Save` / `Restore` of the `[Predicted]` fields, and `Write` / `Read` of the replicated ones.
  - The replicated block holds `[Replicated]` and `[Predicted]` fields, in declaration order, little-endian raw.
  - Read can skip the predicted ones, for the owner.
- **RPCs:** each send stub serializes its arguments into a stack buffer and calls `Net.SendRpc`. A dispatch switch calls `On{Name}`; RPC ids are the methods' ordinal name order.
- **Schema hash:** `__NetSchemaHash`, FNV-1a over the class name, every net field (name, kind, R/P) and every RPC (name, argument kinds, side, target).
- **Kinds:**
  - State: bool, int, float, double, int enums, Vector2, Vector3, Quaternion.
  - RPC arguments: those, plus string and `Entity`. An `Entity` is sent as its NetId.
- **Diagnostics** (errors; nothing is generated):
  - MEGEN004: not partial.
  - MEGEN005: not a `Script`.
  - MEGEN006: nested or generic.
  - MEGEN007: an unsupported, static or readonly net field.
  - MEGEN008: an RPC that isn't a bodiless `partial void`.
  - MEGEN009: an unsupported RPC parameter.
  - MEGEN010: a missing or mismatched `On{Name}`.
  - MEGEN011: a net script deriving from another net script; keep the net members in one class.
- **Dispatch only runs on the side an RPC is addressed to:** a ServerRpc on the server, a ClientRpc on a client, both offline. A client can't make the server run a ClientRpc handler.

**`Net`** (`Modules/ScriptCore/Source/Net/Net.cs`, over the `## Networking` slots of `ScriptAPI.def`):

- **Session:** `Role` / `IsServer` / `IsClient` / `IsActive`, `LocalClientId`, `Tick`, `IsResimulating`, `Clients`.
- **Ownership:** `GetOwner` / `SetOwner` (server) / `IsOwner` / `IsPredicted`.
- **Input and stats:** `Command(entity)`, `Press(action, held)`, `Corrections` / `Matches` / `InitialSyncs`, `GetNetId` / `FindEntity`.
- **Without a session**, everything reads as offline, and RPCs run locally: offline play is both sides.

**Engine side:**

- **Networked actions:** `InputAction.Networked` (JSON `"Networked": true`) marks actions that travel in commands.
  - `NetLaunch` sorts them by name: buttons take the command's bits in that order, Value / Vector2 actions its four axes.
  - The client samples them at the start of each fixed step, before `Net->BeginTick`. A button counts as held, or as pressed since the last step, so a tap shorter than a tick still reaches one command.
  - The engine samples only once something is networked. Otherwise a C++ game's own `NetCore::SetLocalCommand` stands.
  - `WasPressed` compares with the previous tick's command, which also holds in resimulation.
  - `Net.Press` holds a button from a script (tests, bots).
  - The layout's hash is part of the handshake.
- **`Net.Command(entity)`:**
  - **Server:** the entity's owner's command for this tick (and the one before; `NetServer::Client::Previous`).
  - **Owner's client:** its predicted pawn's live command, or the replayed one during a resim.
  - **Otherwise:** `Valid = false`.
- **Who simulates:** scripts on networked entities run `OnFixedUpdate` on the server, and on a client only on its predicted pawn (`NetCore::SimulatesLocally`; the rest follows snapshots). Non-networked scripts run everywhere. `OnUpdate` / `OnLateUpdate` are presentation and run everywhere.
- **Ownership:**
  - `Net.SetOwner` on the server goes out as an **Owner** message. Static entities' owners only travel that way, and late joiners get the current ones.
  - A client that comes to own an entity with a `CharacterController` predicts it (`NetCore::Predict`).
  - Losing it stops the prediction, and the entity follows snapshots again.
  - When a client leaves, what it owned goes back to the server.
- **State on the wire** (`NetCore::ScriptGlue`, filled by `Scripting/ScriptNet.cpp` through the `ScriptBridge` net entry points):
  - **Snapshot fields:** `[u16 reflected size][NetSchema bytes][script bytes]`.
    - Remote copies read all of it.
    - The owner reads the replicated fields of its own pawn too, skipping `[Predicted]`.
  - **OwnerState:** carries the pawn's `[Predicted]` bytes after the mover.
- **Prediction:** `NetPredictor::ScriptHooks`.
  - Each tick's record keeps the `[Predicted]` bytes, which `Reconcile` compares exactly.
  - On a mismatch the server's bytes are restored with the mover. Every replayed tick then runs the pawn's `OnFixedUpdate` before its character step: a live tick's order.
    - During the replay, `Net.IsResimulating` is true and `Net.Tick` is the replayed tick.
    - Every replayed tick runs the pawn's script, whether or not it has `[Predicted]` state, since it may steer the mover from `Net.Command`.
- **RPCs:** a reliable **Rpc** message `{NetId, rpc id, bytes}`, at most 1 KB (arguments up to `Net.MaxRpcArgsBytes`, 1008).
  - An entity the server created this tick has no NetId until the tick ends: RPCs on it are dropped, with a warning. Send them a tick later, or use a `[Replicated]` field.
  - The server runs a client's RPC only on an entity that client owns.
  - `ClientRpc` goes to the owner or to every client.
- **Handshake:** `ScriptSchema` folds in the networked action layout and every networked class's `__NetSchemaHash`, so a client with other script fields or RPCs is rejected (`"scripts"`). Protocol 3.

**Rules of thumb:**

- Don't log, play sounds or send RPCs from `OnFixedUpdate` without checking `Net.IsResimulating`: a resim runs it again.
- Keep `[Predicted]` state deterministic: the same code on the same commands. It is compared byte for byte.
- One `ScriptComponent` per networked entity carries its net state.

### Lag compensation (NET-8)

- **View tick:** each client command carries `ViewTick`, the render tick of its interpolated view when the command was made. It goes on the wire as how far behind the command's tick it was, in 1/64 ticks.
- **Recording:** after each server tick, `NetLagCompensation::Record` stores every `NetHitbox`'s world pose: a capsule along local Y (Height, Radius) or a box (HalfExtents), around a local Center.
- **The shot:** a server command handler calls `NetCore::LagCompensatedRaycast( origin, direction, range, command, shooter )`.
  1. The hitboxes are interpolated to the command's view tick.
  2. The ray is tested analytically against each, skipping the shooter. Nothing in the physics world moves.
  3. A present-day `RaycastAll` finds occluders, ignoring entities with hitboxes, whose bodies are in the present.
  4. A hit behind an occluder comes back `Occluded`.
- **The cap:** a view older than `MaxRewindSeconds` (0.4 s, about 300 ms RTT plus the view's delay and slack) is `Rejected`.
- **Overlays:** with `DrawOverlays`, each shot draws the ray (green on a hit, red otherwise), the rewound hitboxes in yellow and the present ones in gray, for 2 s.

### UDP and dedicated servers (NET-5)

**`UdpTransport`:**

- **Socket:** one non-blocking IPv4 UDP socket, bound to a port (0 picks a free one) on every interface or one address.
- **Receive:** returns false once nothing more has arrived. Datagrams over 1500 bytes are skipped (`NetConnection` packets are at most 1200).
- **Addresses:** a `NetAddress` is `(ip << 16) | port`, so the address a client resolves from `--connect` is bit for bit the `From` its replies carry. `Resolve` asks for IPv4 only, so `localhost` can't come back as `::1`.
- **Platforms:**
  - Winsock on Windows: `PCH.h` includes `winsock2.h` before `Windows.h`, `Ws2_32.lib` links by pragma, and `SIO_UDP_CONNRESET` is off.
  - BSD sockets on Linux and macOS.
- **Binding:** no `SO_REUSEADDR`, so a second process can't silently share a port.
- **Conditions:** `--net-lag` and the other `--net-*` options wrap the socket in a `ConditionedTransport`. `NetCore` drives `Update` from `Receive`, `EndTick` and `Flush`, so held packets go out at frame resolution.

**The Server build** (`CommonTarget.Mode.Server` → `server_<platform>_<optimization>`, e.g. `make -f MitchGame.make Game_EntryPoint config=server_linux_debug` → `.build/Server_linux_Debug/`):

- **Defines:** `DEFINE_ME_HEADLESS`. Debug and Release keep `ME_TOOLS`, so scripts still build beside the exe, while `ME_IMGUI` and `ME_GAME_TOOLS` are off when headless.
- **Window:** `Engine::Init` creates a `NullWindow` instead of an SDL window.
- **Renderer:** runs bgfx's **Noop** backend (`RendererCreationSettings::Type`). Noop loads the platform's own shader binaries (`GetPlatformString`: spirv / metal / dx11) and draws nothing. Cameras, meshes and particles still go through their cores and are culled and sorted on the CPU.
- **Pace:** with no display to wait on, a headless run caps itself at one frame per tick (`1 / FixedTimeStep`) unless `--max-fps` or the config's `MaxFrameRate` says otherwise.
- **UI:** RmlUi gets no renderer, so it never builds a context: no documents load. Values set through `UI.Model` are kept, unread.
- **Audio:** none. FMOD compiles out (`ME_FMOD` needs `!ME_HEADLESS`) and isn't linked or shipped (`CommonTarget.HasAudio`, ME-287): a dedicated server needs no `libfmod`.
- **Where it builds:** Win64, macOS and Linux, never UWP.

**The command line** (`NetLaunch`, any game build):

| Option | Effect |
|--------|--------|
| `--server [port]` | Host on that UDP port (7777 by default; 0 picks a free one and logs it), every interface, at the engine's tick rate (`1 / FixedTimeStep`) |
| `--connect host:port` | Join a server: a dotted IPv4 address or a name that resolves to one |
| `--net-lag ms`, `--net-jitter ms`, `--net-loss f` | Condition what this side sends. Loss is a fraction (`0.05`) or a percentage (`5%`). Both sides add up: `--net-lag 40` on each is 80 ms RTT plus a frame |
| `--net-seed n` | The conditions' seed |
| `--net-timeout s` | Seconds of silence before a peer is gone (10) |
| `--net-report file.json` | On exit, `NetCore::WriteReport`. **Server:** clients accepted / left / timed out / rejected, snapshots sent, and per client: status, connection stats, kbps, snapshots and their rate. **Client:** state, whether it connected, timeouts, RTT and loss, snapshots received and their rate, resyncs, dilation |
| `--net-exit-when-empty` | Server: quit once its last client has gone |

The session starts after the scene loads, because the handshake hashes the scene's networked entities, so both sides need `--scene` (or the same startup scene).

- **Log lines:** `Net: hosting on UDP port …`, `Net: connected as client #1 at 60 Hz`, `Net: client left`, `Net: … timed out`. They are flushed at once, so tools can wait on them.
- **A failed start** (a taken port, an unresolvable host) logs why and quits with exit code 1, and the report carries an `Error`.
- **Headless runs don't write the player's state:** no `Engine.cfg` window or scene, and Retail logs go to `Logs/Server.txt`.
- **On exit:** the report is written first, then Disconnect goes to the other side.

**Playing locally** (`Tools/RunNetLocal.py`, ME-289):

- **What it does:** starts the Server build, waits for it to host, then opens N client windows side by side.
  - Each client is a game build with `--connect`, sized with `--width/--height`, placed with `--window-x/--window-y` and kept `--window-mode windowed`.
  - Each window's title says which client it is (`[client #2]`; `NetLaunch` sets it).
  - The server quits once the last window closes. Ctrl+C stops everything.
  - At the end it prints each side's report: joined / left / timed out, RTT, loss, snapshots per second, kbps.
- **Running it:**
  - `Engine/Tools/RunNetLocal.py [--scene X.lvl] [--clients N] [--port 7777|0] [--net-args "--net-lag 50 --net-loss 2%"] [--build]`.
  - Outside the dev shell it re-runs itself through `Tools/NixEnv.sh`. Logs and reports land in `.tmp/NetLocal/`.
- **VS Code** (the game's and the project template's `.vscode`):
  - Tasks "Net: server + 2 clients" and "Net: server + 2 clients (50 ms, 2% loss)" build both configurations first (`build-net-debug`) and ask for the scene.
  - Run and Debug has the same thing: "Net: server + 2 clients (no debugger)" runs `RunNetLocal.py` in a terminal.
  - The compounds "Net: server + client (debug both)" and "Net: server + 2 clients (debug all)" run the server and one or two clients under gdb together. They use `--net-timeout 60`, so breakpoints don't drop the session.
- **Window placement:** tiling window managers and native Wayland place windows themselves; the offsets matter on stacking desktops.

**The smoke test** (`Tools/RunNetSmoke.py`, the RunChecks "Networking smoke" step):

1. Starts the Server build on a free port (`--server 0`, read back from its log) on `Assets/Scenes/Tests/NetTest.lvl` with `--net-exit-when-empty`. NetTest has six networked boxes that fall and settle in view, and a `NetSpin` cube behind the camera that turns every fixed step, so every tick has a snapshot to send.
2. Waits for the server to host.
3. Runs a client through `Headless.sh` for 900 frames at `--max-fps 60`, then captures and leaves.
4. **Passes when:**
   - the client connected and stayed connected for at least 80% of its run, with no timeouts on either side and no bgfx asserts in either log;
   - snapshots arrived within 5% of the tick rate;
   - the server counted a clean Disconnect and exited on its own;
   - the capture matches `Assets/Scenes/Tests/Reference/NetTest.png`. The same reference serves the standalone test-scene capture: the settled boxes land in the same places offline.
5. `--net-args` passes options to both sides (e.g. `--net-args "--net-lag 40 --net-loss 2%"`), and `--update` takes a new reference.

## Proofs

`NetTests.cpp`:

- **Bit stream:** round-trips widths of 1–32 bits, VarUInt up to 2⁶⁴−1, float bits, bytes and strings. It flags truncation and refuses a hostile string length.
- **Quantization:** floats at 4, 10, 16 and 24 bits come back within half a step, vectors at 20 bits, and 500 random rotations at 12 bits (|dot| > 0.99999).
- **Reliable under a bad link:** 600 messages one way and 150 the other, over 40 ms ± 15 ms with 20% loss, 10% duplicates and 10% reordering, arrive complete, in order and once. Unreliable messages arrive at most once (most of them). The loss estimate reads 10–32%.
- **Commands (`NetSessionTests.cpp`):** delta-coded lists round-trip, including a resync's tick gap. The server buffer serves, repeats, and ignores late copies.
- **Clock:** dilates towards the target, caps at 5%, resyncs when 13 ticks off, and ignores the next stale report.
- **Two worlds, the NET-2 done-when:** 60 s at 50 ± 10 ms each way (100 ± 20 ms RTT) with 5% loss. All 3600 commands arrived on time, ≥ 99.9% required. Worst slack error 0.82 tick (≤ 1), worst dilation 1.6% (≤ 5%), no resyncs. The server simulated exactly the client's commands. After the link slows by 60 ms each way, the slack is back within 1 tick in 10 s.
- **Replication (`NetReplicationTests.cpp`):**
  - `NetSchema` writes exactly the Replicated fields.
  - Deltas carry only changes and gone ids, a wrong baseline is refused, and an unchanged snapshot is ≤ 10 bytes. The budget defers entities, with "sent" matching what's decoded.
- **The NET-3 done-when:** 50 boxes (25 placed, 25 spawned) fall onto a floor on the server, over 100 ± 20 ms RTT with 5% loss.
  - **Mirroring:** at each client frame's render tick, 99.8% of 12000 samples are within 1 cm of the server's pose at that tick (≥ 99.5% required). The rest are boxes landing inside a lost-snapshot gap, which straight-line interpolation can't know about; the worst was 5.5 cm (< 10 cm required). The placed copies are kinematic.
  - **Idle:** once asleep, snapshots cost 480 bit/s (< 1 kbps).
  - **Despawn and late join:** destroying every seventh box, plus a late joiner at 140 ms RTT, converge. Both clients mirror exactly the server's set within 2 mm.
- **Prediction (`NetPredictionTests.cpp`), the NET-4 done-when:**
  - **Resim:** a predictor corrected at tick 19 resims 20 ticks in 0.056 ms (< 1 ms, Debug). Its visual offset is the jump, and it's gone after 100 ms. Resimming from the straight run's own state reproduces that run bit for bit.
  - **Course:** two worlds at 150 ms RTT (75 ± 3 ms each way) run a walk/strafe/jump course with a step and a wall: 697 ticks confirmed, **0 corrections**, after 1 initial sync.
  - **Knockback:** a server-only knockback gives **exactly 1 correction**, at the knockback tick. The visual offset is zero by the 10th frame after it, and no further corrections follow.
- **Relevancy (`NetRelevancyTests.cpp`), the NET-13 done-when:** a 20 s soak of 200 bots wandering a 200 m square, with 4 clients at its corners, a 50 m radius, 12 KB/s each, and 2% loss.
  - **Budget:** every client got exactly 12288 B/s of snapshots, within its budget.
  - **Starvation:** a relevant bot waited at most 3 ticks for an update.
  - **Priority:** bots within 15 m updated 0.97 times a tick, against 0.35 for those 35–50 m out.
  - **Relevancy:** nothing was sent while out of range.
  - **Jobs:** a run encoded on jobs matches a serial run byte for byte.
- **Lag compensation (`NetLagCompensationTests.cpp`), the NET-8 done-when:**
  - **Shapes:** ray vs capsule (side, cap, miss, range, behind) and ray vs a turned box.
  - **Setup:** at 200 ms RTT, a target strafes at 5 m/s while the client fires every 20 ticks, aiming at the target where it sees it.
  - **Results:** of 60 shots, rewound by 16.6 ticks (276 ms) on average:
    - all 50 unobstructed shots hit with rewind;
    - the other 10 were correctly blocked by a wall;
    - 2 hit without rewind, near the target's turnaround;
    - all 60 forged views 30 ticks further back were rejected.
- **Handshake:** another build is rejected (`"version mismatch: build"`) and the server lets it go; so is another scene (`"version mismatch: scene"`); a match connects.
- **UDP and session endings (`NetUdpTests.cpp`):**
  - Addresses resolve (`localhost` too) and print, and malformed ones are refused.
  - Two sockets on 127.0.0.1 trade datagrams up to 1200 bytes, with each `From` equal to the sender's resolved address, and a taken port fails to bind.
  - A `NetServer` and `NetClient` connect over real UDP. Commands arrive, and the client's Disconnect removes it at once (1 disconnect, 0 timeouts).
  - A client drawing at 1000 fps against a 60 Hz server sends 60.3 packets/s, with a loss estimate of 0. Before the cap it sent one packet a frame; past the server's 33-packet ack window those read as 34% loss on a desktop run.
  - On loopback, a silent client times out on the server (`OnClientRemoved` fires once). A client gives up on a silent server and on one that never answers. A closing server tells its client (`"server closed"`), and a leaving client is counted as left, not timed out.
- **Scripts, owners and RPCs (`NetScriptTests.cpp`, NET-6):** two worlds with a C++ stand-in for the C# glue, at 80 ms RTT.
  - **Ownership:** handing the pawn to the client makes it predict it.
  - **RPCs:** a client RPC on what it owns reaches the server, and one on what it doesn't is dropped. Owner and all-clients RPCs arrive, and oversized ones are refused.
  - **Replicated state:** script bytes mirror, the owner reading its own pawn's as owned.
  - **Prediction:** a "dash" counter (+1 a tick, +10 on each Dash press, edges from the previous command) predicts with **0 corrections** over 377 confirmed ticks. A server-only change is **exactly 1** correction at its tick, with the scripts resimulated.
  - **Late joiners** learn the owner.
- **The NET-6 done-when:**
  - **Generator:** 37 tests (`Generators.Tests`): a golden snapshot of a minimal net type, hash stability and sensitivity, every MEGEN004–010, live round trips of a generated probe, dispatch, and zero allocation.
  - **RunNetSmoke with `NetProbe.cs`** (NetTest.lvl's off-camera pawn):
    - The server hands the pawn to the client.
    - The client presses Dash through `Net.Press`, predicts the dash, and pings.
    - The server logs **"ServerRpc ok"**. The client logs **"ClientRpc ok"**, **"Replicated ok"** and **"dash: 0 corrections"**, only once the server-confirmed `Dashes` is 1.
    - It passes on a clean link and at 40 ms + 2% loss each way (RTT 115 ms).
- **The NET-5 done-when (`RunNetSmoke.py`):** the Server build and a client on NetTest.lvl over localhost UDP for 900 client frames (about 15 s).
  - **Clean link:** connected, 0 timeouts, 0 resyncs, 60.06 snapshots/s at 60 Hz (±5% required). The client left cleanly and the server exited by itself. The capture matches its reference exactly (0.00%).
  - **Lossy link:** with `--net-lag 40 --net-jitter 5 --net-loss 2%` on both sides: RTT 111 ms, 59.08 snapshots/s, 0 timeouts, the same capture.
- **Round trip:** at 50 ms each way, 5% loss, with 0 or ±10 ms jitter, both ends estimate within 10% of 100 ms. Garbage and lying packets are rejected, and oversized messages are refused.

## How to Extend

- **A real transport (Steam, NET-9):** implement `INetTransport` as `UdpTransport` does. `Update` can stay empty (`NetCore` calls it anyway), and `NetConnection` doesn't change. Map the transport's peer identities to stable `NetAddress`es: replies must come from the address the client connected to.
- **Bigger messages:** need fragmentation (a reliable slice stream). Until then, keep replicated state under 1 KB a message. Snapshots defer entities past their 900-byte budget; NET-4 adds priorities.
- **A replicated component field:** add `.Replicated()` to its `ME_FIELD`. Clients apply it from the snapshot at the render tick.
- **A networked script:** mark fields `[Replicated]` / `[Predicted]` and methods `[ServerRpc]` / `[ClientRpc]` in a `partial` `Script`, write `On{Name}` handlers, read input with `Net.Command(Entity)`, and mark the actions it reads `"Networked": true` in the `.inputactions`.
- **A new net field kind:** add it to `NetGenerator`'s kind mapping and to `NetWriter` / `NetReader` (fixed-size for state), then update the generator tests (the kind name is part of the schema hash).

## Caveats & Fragility

- **Clock:** the connection trusts the clock it's given. Pass the same monotonic `now` to `SendPacket` and `ReceivePacket`.
- **Reliable window:** a stalled link holds at most 1024 messages in flight. Messages queued beyond that wait in memory, unbounded, until acks free the window.
- **No encryption or authentication:** the protocol id only filters strays.
- **NetSchema is positional:** both sides must have the same components with the same replicated fields, in the same order. The C++ side isn't hashed into the handshake (the build hash covers it for the same game); C# net fields and RPCs are (NET-6).
- **Script prediction is exact:** `[Predicted]` bytes compare bit for bit. Float code that differs across platforms (a Windows client on a Linux server) would read as corrections; quantize such state, or keep it in ints.
- **Hot reload during a session** changes the schema hash for new joiners only, and drops the generated state of reloaded instances back to their saved public fields.
- **Spawned prefabs:** clients create spawned entities from `NetIdentity::Prefab`, which must exist on the client.
- **UDP is IPv4 only** (a `NetAddress` is 64 bits).
  - On a multi-homed server bound to every interface, a reply can leave from another address than the one the client dialled, and the client then ignores it. Bind one address, or connect to the address it answers from.
  - There is no NAT traversal; Steam SDR (NET-9) brings that.
- **Headless servers still prepare frames:** with Noop, cameras, culling and sorting run every frame on the CPU and draw nothing. A dedicated server could skip that work.
- **Ticks follow the engine's fixed steps.** Don't give a server `--frame-time`: its ticks then follow frames rather than the clock, so any stall or speed-up shows up as client resyncs. Real-time servers step by the clock and catch up (`KeepsBacklog`).

## Related Docs

- [Physics.md](Physics.md): the character NetState and SimTick (NET-0).
- [Scripting-DotNet.md](Scripting-DotNet.md): `ScriptBindings::WorldScope`.
- [Architecture.md](Architecture.md): the frame these hooks sit in.
