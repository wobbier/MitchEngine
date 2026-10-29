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

Real transports (ENet, NET-5), the C# API (NET-6) and the editor tooling (NET-7) come next.

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
| `Source/Cores/NetCore.h` | Engine-owned (`Engine::Net`): `Host` / `Connect`, the frame hooks, the version of its world |
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
| `Net->Flush( now )` | Before the frame limiter | The client's packet |
| `GetClockScale()` | The fixed-step accumulator | Scaled by it: dilation without changing `FixedDeltaTime` |
| `KeepsBacklog()` | Backlog drop | A server catches up instead of dropping steps |

**Defaults:** the target slack is 3 ticks. That covers ±10 ms of jitter plus two lost packets in a row at 60 Hz.

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

**`NetSchema` field types:** bool, ints and enums (VarUInt), float, double, Vector2/3/4, Quaternion (raw floats), and strings. Flag fields with `ME_FIELD( X ).Replicated()`. `.Predicted()` implies Replicated and is for NET-5.

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
- Prediction of `[Predicted]` script fields comes with NET-6.

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
- **Round trip:** at 50 ms each way, 5% loss, with 0 or ±10 ms jitter, both ends estimate within 10% of 100 ms. Garbage and lying packets are rejected, and oversized messages are refused.

## How to Extend

- **A real transport (Steam, UDP):** implement `INetTransport`. `Update` can stay empty, and `NetConnection` doesn't change.
- **Bigger messages:** need fragmentation (a reliable slice stream). Until then, keep replicated state under 1 KB a message. Snapshots defer entities past their 900-byte budget; NET-4 adds priorities.
- **A replicated component field:** add `.Replicated()` to its `ME_FIELD`. Clients apply it from the snapshot at the render tick.

## Caveats & Fragility

- **Clock:** the connection trusts the clock it's given. Pass the same monotonic `now` to `SendPacket` and `ReceivePacket`.
- **Reliable window:** a stalled link holds at most 1024 messages in flight. Messages queued beyond that wait in memory, unbounded, until acks free the window.
- **No encryption or authentication:** the protocol id only filters strays.
- **NetSchema is positional:** both sides must have the same components with the same replicated fields, in the same order. NET-3 doesn't hash that into the handshake yet; the build hash covers it for the same game.
- **Spawned prefabs:** clients create spawned entities from `NetIdentity::Prefab`, which must exist on the client.

## Related Docs

- [Physics.md](Physics.md): the character NetState and SimTick (NET-0).
- [Scripting-DotNet.md](Scripting-DotNet.md): `ScriptBindings::WorldScope`.
- [Architecture.md](Architecture.md): the frame these hooks sit in.
