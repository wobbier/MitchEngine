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
| `Source/Net/NetVersion.h` | What both sides must agree on: protocol, build (product name and version), C# script classes, mod set (the net-required mods, sent as `id@version` so a rejection lists the differences; MOD-3, protocol 5), placed entities (sorted GUIDs). FNV-1a hashes, the same on every platform |
| `Source/Net/NetCommands.h` | `InputCommand` (tick, buttons, 4 axes at 16 bits, view tick, optional absolute aim: yaw and pitch at 16 bits each), delta-coded lists of up to 16, and `NetCommandBuffer`, the server's per-client queue (with capped repeats; it follows a client's clock back) |
| `Source/Net/NetClock.h` | The client's command-frame clock: adaptive target slack (NET-14), dilation up to ±5%, a resync beyond 8 ticks or more than a tick behind, and holding still through a stall |
| `Source/Net/NetSession.h` | `NetServer` and `NetClient`: the handshake, commands up, and server reports down |
| `Source/Components/Networking/NetIdentity.h` | Marks an entity networked: NetId, owner, the prefab clients spawn |
| `Source/Net/NetSnapshot.h` | `NetEntityState`: pose at 1/1024 m, rotation smallest-three at 12 bits, replicated field bytes. `NetSchema` (fields flagged `.Replicated()` in `ME_REFLECT`). `NetSnapshotCodec`: deltas against a baseline |
| `Source/Net/NetReplication.h` | `NetReplicationServer` (ids, spawns, snapshots per client) and `NetReplicationClient` (mirrors, interpolation) |
| `Source/Net/NetPrediction.h` | `NetPredictor`: a 128-tick history of {command, mover state}, `Reconcile` (millimetre compare, then restore and resim with `PhysicsCore::StepCharacter`), and visual error smoothing |
| `Source/Components/Networking/NetHitbox.h` | A capsule or box that lag-compensated shots test against |
| `Source/Net/NetLagCompensation.h` | Each tick's hitbox poses for `MaxRewindSeconds`; `Raycast` at a view tick (analytic ray vs capsule / OBB, present colliders occlude); overlays |
| `Source/Cores/NetCore.h` | Engine-owned (`Engine::Net`): `Host` / `Connect` / `Close`, `IsActive` (until `Shutdown`) / `IsSessionLive` (until `Close` or the session's end), the frame hooks (they drive the transport's `Update`), the version of its world, `WriteReport` |
| `Source/Net/UdpTransport.h` | `UdpTransport`: non-blocking IPv4 UDP (BSD sockets / Winsock). Addresses pack `(ip << 16) \| port`; `Resolve( "host:port" )`, `ToString` |
| `Source/Engine/NetLaunch.h` | The command line (`--server`, `--connect`, `--net-*`): starts the session after the scene loads, logs it, writes the report, says goodbye on exit |
| `Source/Engine/ScriptReloadGate.h` | Pauses script hot reload while the process's session is live, and rebuilds what was edited once it ends (ME-311) |
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
- **Reliable messages:** new ones, plus any unacked for a little over a round trip (`1.25 × RTT + 2 × deviation`, at least 20 ms; 100 ms before the first estimate). They go oldest first, within a 1024-message window and the 1200-byte packet budget, and, after the first of them in a packet, they leave room for the largest unreliable message queued that tick (a snapshot, owner state), so a late joiner's burst of Spawns never starves the state that goes in this packet or not at all, while the head of the reliable stream still goes out every packet.
- **Unreliable messages:** the ones queued since the last packet. Each goes in this packet or is dropped.

On receive, the connection works through these steps:

1. **Validate:** a packet with the wrong protocol id or a malformed body is rejected whole, before anything is applied.
2. **Duplicates:** a sequence it has seen, or one older than the ack window, is dropped as a duplicate.
3. **Acks:** these settle the sent-packet ring. Each settled packet's reliable messages stop resending.
4. **Round trip:** taken from the newest acked packet, minus the ack delay, then smoothed (1/8 gain, deviation 1/4). A packet read long after this end last sent gives no sample: it sat waiting on a hitch here (a long frame, a breakpoint), and that wait isn't the link's. Otherwise one hitch would read as a round trip of its length and a deviation of a quarter of it. "Long" is 50 ms or three of this end's usual gaps between packets (smoothed, each gap counted up to 0.5 s), whichever is more: an end that sends seldom (a 5 fps client, a 10 Hz server) must keep sampling, or its first sample (deviation: half the round trip) would stand for good. (At 100 ms a 0.05 s client hitch got through: its sample took the deviation on a 20 ms link to 14 ms, and the slack target's jitter floor to 2 ticks for seconds; ME-313.) A client also gives none from packets read more than two of its usual gaps between reads (frames: `NetConnection::BeginRead`, smoothed) after the read before: a hitch inside a frame, after its read and before its send, sends just before the next read, so the last send says nothing; the packets that came meanwhile sat unread (an in-frame 0.05 s hitch took the floor to 2 that way).
5. **Loss:** a packet that leaves the 33-packet window unacked counts as lost (`PacketsLost`, a smoothed `PacketLoss`).
6. **Reliable delivery:** messages are delivered in id order exactly once. Early ones wait for the gap to fill.

Messages are at most 1024 bytes (`kMaxMessageBytes`); there is no fragmentation yet, and a session logs (once a second) any message it had to drop for being larger, instead of silently never sending it. Sequences and message ids wrap at 16 bits (`NetSequenceGreater`). For the session above it, the connection says of the packet it just took whether it was the newest (`WasLastPacketNewest`: no later one had arrived; every packet, a keep-alive too, moves that on, so it never wraps however long a link idles) and what of this end's packets it acked (`GetLastPacketAck`).

### Sessions (NET-2)

1. **Hello:** the client sends Hello with its `NetVersion` on the reliable stream. A server whose version differs answers **Reject** with what differs (`"version mismatch: build scene"`). Otherwise it answers **Welcome**: client id, tick rate, its tick (past the ticks it still owes, when it's catching up after a hitch). A rejected client is dropped once the Reject is acked, or after 2 s; a connection that never says hello is dropped after 10 s.
2. **Start tick:** on Welcome, the client starts its clock at `server tick + RTT in ticks + target slack`, its ticks the server's length. The server is half a round trip past that tick by then; the client runs half a round trip further ahead, plus the slack.
3. **Commands up:** each client tick stamps the local command (`NetCore::SetLocalCommand`, later the HK-5 sampler) with the clock's tick. Each client packet carries the newest unconfirmed commands, up to 16, delta-coded, so a lost packet's commands ride the next ones.
4. **Server ticks:** each tick, the server takes every client's command for that tick from its `NetCommandBuffer`. A missing command repeats the previous one (a miss), and the server never stalls. The ticks before the client's first command are idle, not misses: its clock starts a few ticks ahead.
   - **Capped repeats:** a quarter second of misses in a row (`NetServer::kDefaultCommandRepeatSeconds` at the tick rate: 15 ticks at 60 Hz, 8 at 30) repeats the last command whole. Past that, the repeat keeps its aim and view tick but lets go of every button and axis, so a client that froze, crashed, sat at a breakpoint or lost its link while holding move and fire stops moving and firing instead of running on until the timeout.
   - **Resetting:** the next command that arrives in time ends the run. `NetServer::SetMaxCommandRepeats` changes the cap for every client, now and later (0: neutral from the first miss); `NetCommandBuffer::GetRepeatRun` and `GetNeutralCount` show it working.
5. **Slack:** measured at each consume as `newest tick received − (tick + ticks owed)`, then smoothed (1/10, or taken at once after a jump of 4 or more). The newest tick counts commands that arrived too late to run, so a client whose clock is behind reads as behind (negative slack), not as silent. (A client whose first commands were all late used to leave the buffer empty and the reports silent about it: it stayed behind for good.) The ticks owed are a catching-up server's (below). Commands more than 256 ticks past the server's tick are dropped.
6. **Reports down:** each server packet reports to each client the tick, the newest command it has, that client's slack (14 bits within ±64 ticks; past that, a bit says so and it goes in whole ticks, 32 bits: protocol 7), how many of its commands the server has had to repeat so far (protocol 4), and how long it has waited for the client's next command: since a packet with a new one arrived, in ms (protocol 7).
7. **Clock control:** the client notes every report in order (confirmations, misses) and steers by the newest of each `Receive`: `NetClock` sets dilation to `−(slack − target) × 0.02`, clamped to ±5%. More than 8 ticks off, or behind (below), it jumps instead (a resync), unless the report is a stall's (below). Past ±64 ticks (a client paused for minutes, a server that lost seconds to a hitch) the slack comes in whole ticks, measured the same way.

**Protocol 7** (the clock fix): on the wire, `ServerState` gains a bit before the slack (clear: whole ticks in 32 bits instead of the 14 quantized) and, after the misses, the server's wait (a VarUInt, ms). What fields mean changed too: the newest counts late commands, the slack is measured against the caught-up tick, the server rewinds a client's buffer when its clock goes back, and Welcome's tick is past the ticks owed. A client of this protocol on an older server would misread it (a client behind would read every report as a stall and never resync), so the build hash, which folds the protocol in, keeps them apart.

**Frame hooks** (`Engine::Run`; all no-ops without a session):

| Hook | Where | Effect |
|------|-------|--------|
| `Net->Receive( now )` | After the frame's events | Packets in |
| `Net->BeginTick( owed )` | Start of each fixed step | The server takes commands; the client stamps its command. `owed`: the steps still due after this one (a server catching up after a hitch) |
| `Net->EndTick( now )` | After the physics step | The server reports, one packet per client |
| `Net->Flush( now )` | Before the frame limiter | The client's packet: at most about one a tick (each new command sends; otherwise a keep-alive after two ticks without one), however fast it draws |
| `GetClockScale()` | The fixed-step accumulator | Scaled by it: dilation without changing `FixedDeltaTime` |
| `KeepsBacklog()` | Frame clamp and backlog drop | A server catches up on up to `NetServer::kMaxCatchUpSeconds` (0.5 s; past the 0.25 s frame clamp) instead of dropping steps, while its steps run faster than real time |

**Stalls and outages:** the slack only says where the clock is while the server hears the client. When the uplink drops out or slows (a Wi-Fi scan), or the client itself hitches, the newest command the server has stops moving, and the slack sinks a tick per tick with the wait. Read as a clock error, that sent the client ahead in jumps (about 134 ticks after a 1.5 s outage), then back too far once the link returned, and the server ran commands a second stale. So the client checks each report first:

- **Stalled:** the report's wait (the server measures it: since a packet with a new command last arrived) is past the usual: one send interval (smoothed, at least a tick) plus 2 ticks plus twice the round trip's deviation, that last at most 2 ticks. (A latency spike inflates the deviation, and mustn't pass the wait it caused off as usual. The deviation is the one from before the `Receive`, for the same reason.) Measured by the server, the wait is exact whatever the link does both ways at once: a client estimating it from its own send times and the round trip read a two-way spike's held reports as fresh, and resynced on them.
- **A stalled report steers nothing:** no dilation, no resync. When commands get through again they land where they did before, so the slack is right at once.
- **Gaps and outages:** the misses in a gap (the server waiting for the client's next command) are held until it ends: the newest moves and the wait is short again. A gap a deeper buffer could have covered grows the target then (NET-14: a burst of loss, a latency spike). One no buffer covers is an **outage**: a wait past the usual plus `MaxSlack` ticks, or, when the reports from the gap were lost, more misses than that less the buffer it ran down first. An outage's misses don't grow the target: the repeats (capped at a quarter second) cover it, and a deeper buffer wouldn't have. So the target is never deeper during or after an outage, not even for a moment.
- **Quiet:** with no usable report for `QuietTicks` (10), the clock stops dilating (the last report is getting old) and the time doesn't count as clean for shrinking the target. That covers a dead downlink, which sends no reports at all.
- **Hitches:** a long frame or a breakpoint on the client is a stall too, as the server sees it, not a clock error (and an outage, past `MaxSlack`). The round trip samples it would spoil are skipped (above). The engine runs at most 8 steps a frame and drops the rest, so a hitch past 8 ticks and a frame (0.15 s at 60 Hz) comes back behind: its commands late, which the next report says (below, **Behind**).
- **Its own hitches don't grow the target (ME-313):** a shorter one, caught up in full, leaves a gap a deeper buffer would have covered, but the gap is the client's, not the link's, and buffering for it would hold every command that much longer from then on. The client knows when it hitched: a packet with commands going out more than two ticks past its usual interval (smoothed, at least a tick; once it has 20 samples, as it starts from 0) carries a burst of ticks made late. When the gap the burst ends is over, its misses on the burst's ticks (one a server tick at most: they're counted against the ticks between the gap's reports) are the hitch's, and don't grow the target; any others in it do. Most hitches come inside a frame (Update, Render, after the frame's ticks and before its send): the late packet then carries only that frame's ticks, and the ticks due during the stall come in the next frame's packet, so the burst stays open for the next frame's first ticks (as many as the time from the frame's read to its send covers). Until the server has heard all of a burst its reports are about the hitch's gap, not the clock (the one on the late frame's tick alone reads as the clock 7 ticks behind after 0.14 s, and jumped it): the client holds the clock through them, as through a stall. Back to back (stutter) the bursts come faster than the round trip, so the client keeps up to 8 unheard ones (adjoining ones join); keeping only the newest, a second hitch before the report on the first took the first's misses to the target (0.1 s then 0.05 s on a 50 ms link: to 6). A Debug client hitches like this whenever it instantiates prefabs (spawns, effects). On a 20 ms link 0.05 s used to take the target from 1 to 4, 0.1 s to 6 and 0.15–0.17 s to 8 (`MaxSlack`), easing back over 20 s and more: about 100 ms more input latency after one hitch.
- **Counting:** `NetClient::GetStallCount`, `GetStalledReports`, `GetOutageCount`, `GetHitchCount` and `GetHitchMisses`; the client's JSON report has `Stalls`, `Outages`, `Hitches` and `HitchMisses`, and the net debug panel shows the hitches.

**A server hitch:** a server that stalls (a Debug server loading a joiner's world, say) then runs the ticks it owes, 8 a frame (`Engine::Run`'s cap; a headless server draws a frame a tick), sending a report after each. Measured against the step it's on, the slack read as every client far ahead until it caught up: they jumped back, then forward again, with misses between. So the slack is measured against where the server will be once caught up: `newest − (tick + ticks owed)`, the owed steps coming from the engine's fixed-step accumulator (`NetCore::BeginTick( owed )`). A client's clock kept time while the server didn't, and the reports say so.

- **How much:** `Engine::Run` lets a server (`KeepsBacklog`) past the 0.25 s frame clamp up to `NetServer::kMaxCatchUpSeconds` (0.5 s), keeps at most that much backlog, and `BeginTick` caps the ticks owed to it. A longer hitch loses the rest for good: every client reads its clock as that far ahead and resyncs back once (the server rewinds what they made past it).
- **Past ±64 ticks** (a hitch of about 1.5 s or more at 60 Hz) the slack goes in whole ticks, measured the same way. It used to be read as the newest command against the report's tick instead, which leaves out the ticks still owed; the report a client steers by comes from the first frame of the catch-up, with about 22 still owed. Clients jumped back that much too far: their commands came late through the 30-tick cooldown (neutral after 15 misses), the server ran commands they later made again, and a second resync went forward (a 3 s hitch: 2 resyncs, 50–57 misses, the target up to 8).
- **Very long hitches:** commands more than 256 ticks past the server's tick are dropped, so after a hitch that long (about 4.5 s at 60 Hz, 2.5 s at 128 Hz) the newest the server has falls short of the client's: the first jump back falls short and a second one follows after the cooldown (both back, no misses, nothing run the client took back).
- **A server that can't keep up** (its steps take longer than they cover, say a Debug build under load) drops its backlog instead of holding it: held, it would hold its clients' clocks ahead of what it simulates by the backlog, input latency for nothing. Its clients follow where it is, resyncing back each time they get 8 ticks ahead.

**Resyncs:** a report more than 8 ticks off target jumps the clock by the error, now only for a real offset: the link's latency changed by more than 8 ticks, the client came back behind from a hitch (the engine runs at most 8 steps a frame and drops the rest), or the server lost ticks to a hitch past 0.5 s.

- **Behind:** a slack below −1 tick (`BehindTicks`) jumps to the target too, however close the target. Every command then arrives after its tick, each one a miss, and dilating back at 5% takes 20 ticks a tick: a client hitch of 0.18–0.25 s (8 steps caught up, 3–7 ticks behind) used to miss every tick on its return until those misses had grown the target (to 7 after 0.18 s) and it resynced anyway, then ease back for 20 s or more. Both the smoothed slack and the slack the newest command arrived with (the newest against the report's tick, plus the server's wait in ticks) must say so: the smoothed one lags a gap's end, and sinks through a gap's first ticks before the wait marks the report stalled. The jump goes by the lesser of the two.
- **Back from its own hitch,** the first report to steer once the server has heard all of a hitch's burst jumps if the clock is behind at all (`NetClock::OnSlackReport`'s behind threshold 0 rather than `BehindTicks`). Just past the engine's catch-up (8 steps and a frame: 0.155–0.175 s at 60 Hz, by the frame's phase) a hitch drops under a tick and a half, which a 1-tick target leaves up to a tick behind: inside `BehindTicks`, so it dilated back over about 30 ticks with every command late, and those misses took the target to 7 after 0.17 s (before ME-313 they took it to 8, deep enough to resync anyway). A hitch caught up in full comes back at the target, and a link spike isn't the client's hitch, so neither jumps.
- **After a jump** the clock ignores reports for 30 ticks, and the client ignores them until one comes in a packet that acks the client's first packet with a command made since (`NetConnection::GetLastPacketAck`): before that the server hadn't heard the new clock, and they describe the old one. They confirm nothing (after a jump back, the newest they name is a tick the client has made again, whose new command must keep riding its packets), and their misses are the old clock's, so they don't grow the target.
- **The cooldown's misses:** the client stops ignoring reports about a round trip after a jump, but the clock can't steer until its 30 ticks are up (`NetClock::InCooldown`). Misses in between are the jump's (it landed short, or the clock is still off), not the buffer's: they don't grow the target either. (A client starting 35 ticks behind used to grow its target from 4 to 8 in its cooldown, and jump to 8.)
- **Forward:** the ticks it skips would reach the server as misses (repeats of the last command, and target growth). The client sends its last command for the last 15 of them instead, so the server reads an unbroken stream.
- **Back:** the client makes ticks it already sent again. Its next packet ends on a tick older than the newest the server has; the server drops what it had past that tick (made for a timeline the client left, `NetCommandBuffer::GetRewindCount`), and the commands made again replace the old ones. Only the newest packet speaks for the clock (`NetConnection::WasLastPacketNewest`): a reordered older one only fills gaps up to the newest tick. The server never runs a command the client took back.

**Adaptive slack (NET-14, the Overwatch loop):**
- **Start:** the target starts at 3 ticks.
- **Grows:** each command the server reports having repeated (a miss) raises the target by a tick, up to `MaxSlack` (8), counted when its gap ends. Not an outage's misses, nor a resync's, nor any in a resync's cooldown, nor the client's own hitch's (above). So repeated latency spikes (Wi-Fi background scans) cost misses once or twice, then the buffer covers them.
- **Shrinks:** after `ShrinkAfterSeconds` (3 s) with no misses, it eases down by a quarter tick a second, while reports come. The floor is `max(MinSlack, 2 × jitter in ticks)`, where jitter is the connection's RTT deviation.
- **Where it settles:** on a clean link the buffer holds about 1 to 1.5 ticks, so input latency is as low as the link allows. On a 40 ms link with 2% loss it holds about 2.5 ticks.
- **Turning it off:** `AdaptiveSlack = false` keeps the fixed 3-tick target.
- **The debug report:** `NetCore`'s client report shows `TargetSlack`.

**Late presses:** a command that arrives after its tick already ran on a repeat isn't simply dropped. Any button it pressed that the repeat didn't have is merged into the next simulated tick, for that tick only (`NetCommandBuffer::GetRecoveredPresses`). A tap arrives a tick or so late instead of never, and the client, which predicted it, sees at worst a small correction. A button the client held through the gap isn't a press, even where a capped repeat had let go of it.

**Ending (NET-5, protocol 2):**

- **Disconnect:** a leaving client (`NetClient::Disconnect`, `NetCore::Close`) or a closing server (`NetServer::Close`) sends **Disconnect** with a reason. It is unreliable, since the sender is going, so it goes in three packets of its own. The server drops that client on its next `Receive` and counts it (`GetDisconnectCount`); the client becomes `Disconnected`, and `GetDisconnectReason()` says why (`"left"`, `"server closed"`).
- **Timeouts:** a peer that sends nothing for the timeout (10 s, `SetTimeout`, `--net-timeout`) is gone.
  - The server drops a silent accepted client and counts it (`GetTimeoutCount`). It still says Disconnect (`"timed out"`) on that client's own connection, in case the client is alive after all.
  - The server only sends to addresses that said hello. Packets from a session it has dropped get no answer, so such a client times out instead of hanging on half-open.
  - A client stops at `State::TimedOut`, counted from its Hello first going out, including while still connecting to a server that never answers.
- **One session per `NetClient`:** `Connect` works once; reconnecting needs a new client. Client ids (1–255) are reused only once no accepted client holds them.
- **`OnClientRemoved`:** fires for either, before the client goes. Replication forgets that client's state there, and `NetCore` keeps a summary for its report.
- **Session events for gameplay:** `NetCore` fires a typed `NetSessionEvent` (`Source/Cores/NetCore.h`) for who came and went, so every listener hears every event: `Listen<NetSessionEvent>` in an `EventReceiver`, or `EventSubscription::Create<NetSessionEvent>`, and check `Source` against your world (several worlds can hold sessions in one process). `ScriptCore` is one such listener and hands its world's to C# (below).
  - **When:** on the main thread, once the `NetCore::Receive` or `Close` that noticed them has finished (never from inside the session's loops, so a listener may even `Close`). `Engine::Run` calls `Receive` before the frame's fixed steps and scripts.
  - **The server:** `ClientJoined` once replication has sent the new client the world, `ClientLeft` with `"left"`, `"timed out"` or `"closed"` (its own `Close`).
  - **A client:** `Connected` with its id, then `Disconnected` with `"left"` (its own `Close`), what the server said (`"server closed"`, `"timed out"`), `"timed out"` (the server went quiet) or `"rejected: <why>"`.
  - **Reasons from the other side** are read with `BitReader::ReadText`: anything that isn't UTF-8 arrives as U+FFFD, so a hostile server's goodbye can't break the JSON that carries it to scripts (whose dump also replaces rather than throws).
  - `Engine::Run` closes the session only after its loop ends, so `"closed"` and a client's own `"left"` reach C++ listeners still alive then, never scripts.

### Replication (NET-3)

**Ids:**

- **Static:** placed `NetIdentity` entities get NetIds 1..N on both sides, in GUID order. The handshake's scene hash covers exactly these entities, so the orders match.
- **Dynamic:** entities the server gives a `NetIdentity` during the session get ids from `0x10000`. A reliable **Spawn** `{NetId, owner, prefab, pose}` goes to every client; the client creates it from `Prefab`, or as a bare entity with a Transform. A `NetIdentity` without a `Prefab` of its own takes the asset the entity was instanced from (`World.Instantiate` / `CreateFromPrefab`), so a prefab spawned from C# replicates as that prefab. `NetIdentity` is a registered script component: C# has typed wrappers, `entity.AddComponent<NetIdentity>()` with `Prefab`, `Priority`, `AlwaysRelevant`, `NetId` and `Owner`, and `NetHitbox` with `Shape`, `Center`, `Radius`, `Height`, `HalfExtents`, `Occluder` (ME-312). `hitbox.Enabled = false` takes a hitbox out of shots from the server's next recorded tick (a dead pawn). A `NetIdentity` itself can't be turned off from a script (refused with a warning): replication would keep sending it while `BeginTick` stopped routing its owner's commands; deactivate or destroy the entity instead.
- **Despawn:** destroying a networked entity sends a reliable Despawn.
- **Late joiners:** they get Spawns for every dynamic entity, and Despawns for the static ones already destroyed.

**Snapshots:**

1. **Capture:** after each server tick, `CaptureTick` records every networked entity's `NetEntityState` (quantized world pose plus `NetSchema` bytes).
2. **Delta:** each client's packet carries a delta against the newest snapshot that client acked (`SnapshotAck`, sent every client packet), or the full state with no baseline. The server keeps the last 128 sent per client (`kSentHistory`, ~2 s at 60 Hz: the baseline a delta uses is acked half a round trip after it was sent, so a baseline older than the history means full state every tick for that client, as for a fresh join). Changed entities write only the parts that changed, gone ones are listed, and the 900-byte budget defers the rest; the "sent" copy records what the client will really have.
3. **Skip when idle:** a snapshot identical to the last one sent is skipped, with a keep-alive every 15 ticks. The next snapshot names the previous one sent, so a client that has it knows entities held still in between (it adds a hold sample) rather than gliding from an old pose.

**Clients:**

- **Baselines:** decoded snapshots are kept (the last 128, `kReceivedHistory`, matching the server's `kSentHistory`) as delta baselines: the server's delta against the tick the client last acked arrives about a round trip after that tick, so both histories have to outlast a round trip (~2 s at 60 Hz; the old 32 covered ~530 ms, past which every snapshot was full state).
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
  - **The visual jump** (where it was drawn minus where it now is) becomes `GetVisualOffset()`, which shrinks linearly to zero over 100 ms. A jump longer than `MaxSmoothDistance` (2 m: the server teleported the pawn, a respawn) snaps instead: the offset stays zero, so the pawn doesn't glide across the level.
  - **Before the first match**, a mismatch is the initial sync, not a correction: the server simulated idle ticks before the client's first command arrived.
- **Interpolation:** the predicted pawn is excluded from snapshot interpolation.

**Not covered yet:**
- The visual offset isn't applied to anything. A game draws the pawn's mesh (and its first-person camera) at `Transform + GetVisualOffset()`; C# reads it as `Net.VisualOffset( pawn )`. Moving the Transform itself would read as a teleport to the mover.
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
  - MEGEN008: an RPC that isn't a bodiless `partial void`, or one named like an RPC on a base script.
  - MEGEN009: an unsupported RPC parameter.
  - MEGEN010: a missing or mismatched `On{Name}`.
- **Inheritance (ME-294):** a net script can derive from another (directly or through plain classes), and both layers can have net members.
  - **The interface:** only the root net script implements `INetScript`. Each layer generates `__Net*Layer` methods that a derived layer overrides, calling the base's first.
  - **Layout:** the base's state comes first, then the derived layer's appended.
  - **RPC ids:** a derived layer's ids start after the base's (`base.__NetRpcCount + i`), and its `On{Name}` handlers can stay private.
  - **Hash:** a derived `__NetSchemaHash` folds in its base's.
  - **Sealed:** a sealed root script gets private, non-virtual layers.
  - **No shared names:** a derived RPC can't reuse a base RPC's name (MEGEN008). MEGEN011, which used to reject the case, is retired.
- **Dispatch only runs on the side an RPC is addressed to:** a ServerRpc on the server, a ClientRpc on a client, both offline. A client can't make the server run a ClientRpc handler.

**`Net`** (`Modules/ScriptCore/Source/Net/Net.cs`, over the `## Networking` slots of `ScriptAPI.def`):

- **Session:** `Role` / `IsServer` / `IsClient` / `IsActive`, `LocalClientId`, `Tick`, `IsResimulating`, `Clients`.
- **Ownership:** `GetOwner` / `SetOwner` (server) / `IsOwner` / `IsPredicted`.
- **Input and stats:** `Command(entity)`, `LocalCommand`, `Press(action, held)`, `ButtonIndex(action)` / `AxisIndex(action)`, `Corrections` / `Matches` / `InitialSyncs`, `GetNetId` / `FindEntity`, `VisualOffset(pawn)`.
  - **`NetCommand`** reads buttons by name (`IsDown` / `WasPressed` / `WasReleased`) and axes by name (`Vector2("Move")`, `Value("Throttle")`; zero for an action not in the layout), or by index (`Axis(i)`).
  - **Building commands** (bots, tests): `Buttons |= 1 << Net.ButtonIndex("Fire")`, `Axis0`.. from `Net.AxisIndex("Move")` (a Vector2 takes two axes in a row), `PreviousButtons` = the last tick's, `Valid` and `HasAim` + `Yaw` / `Pitch`. The layout is the same on every peer, offline too.
- **`Net.LocalCommand`:** offline (no session; the Player, the editor's Play), the engine samples the networked actions into it every fixed step, before the scripts' `OnFixedUpdate`, exactly as a client samples its commands: `Net.Press` and the `Net.SetAim` aim included, axes quantized as the server would read them, `Tick` counting the steps. `PreviousButtons` is the step before, so `WasPressed` is an edge. `Valid` while any action is networked (or `Press` / `SetAim` holds something); a step that samples nothing clears it, so it never reads stale. The editor's Stop releases `Press` holds and the aim (`NetLaunch::ResetLocalInput`), so the next Play starts with none. So a pawn simulates from one command type online and offline: `var c = Net.IsActive ? Net.Command(Entity) : Net.LocalCommand;`.
  - **On a client:** its predicted pawn's command (a replayed tick's during a resim), or before it has one, the command it sends.
  - **On the server:** `Valid = false`; it has no local player.
- **Session events (EventBus):** `NetClientJoined(ClientId)` and `NetClientLeft(ClientId, Reason)` on the server, `NetConnected(ClientId)` and `NetDisconnected(Reason)` on a client (`ScriptCore` listens to `NetSessionEvent` for its world and queues them with the other engine events, delivered before the frame's `OnUpdate`). The server's own shutdown (`"closed"`) and a client's own quit (`"left"`) raise none: scripts have stopped by then. Subscribe with `On<NetClientJoined>(e => ...)` in a `Script`. A server spawns what a new client plays with there and hands it over with `SetOwner`; a leaver's entities stay (owned by the server again) until the game destroys them.
- **`CharacterController.Position`:** the simulated capsule (`NetState`'s position plus `Center`) after the last step, a `Teleport` or a correction. Read it in `OnFixedUpdate` (eyes, muzzles, distances): it's the same on the server, the owner and in resims, while the Transform is the pose interpolated between steps.
- **Without a session**, everything reads as offline, and RPCs run locally: offline play is both sides.

**Engine side:**

- **Networked actions:** `InputAction.Networked` (JSON `"Networked": true`) marks actions that travel in commands.
  - `NetLaunch` sorts them by name: buttons take the command's bits in that order, Value / Vector2 actions its four axes.
  - The client samples them at the start of each fixed step, before `Net->BeginTick`. A button counts as held, or as pressed since the last step, so a tap shorter than a tick still reaches one command.
  - The engine samples only once something is networked. Otherwise a C++ game's own `NetCore::SetLocalCommand` stands.
  - Offline it samples the same way into `NetLaunch::RecordLocalCommand` (C#'s `Net.LocalCommand`; a client records its commands there too). Without a session the layout follows the action map's hot reloads (`NetLaunch::RefreshCommandLayout`, against `InputActionSystem::GetMapVersion`); `Net.ButtonIndex` / `AxisIndex` caches follow `NetLaunch::GetLayoutVersion`. A session's layout is fixed.
  - `WasPressed` compares with the previous tick's command, which also holds in resimulation.
  - `Net.Press` holds a button from a script (tests, bots), until it's released or the editor's Play stops.
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
- **`Net.PredictedEvent(this, eventId, value)`** (ME-291) is the way to raise FX and sounds from `OnFixedUpdate`; they arrive in `Script.OnNetEvent(eventId, value)`.
  - **Offline:** it presents right away.
  - **The server:** it presents right away and sends a copy to every client under the reserved RPC id `0x3FFFFFFF`, carrying its tick.
  - **The predicting owner:** it presents once per (entity, tick, event) (`NetEventLedger`, about the last 256 ticks). Resims of that tick, and the server's copy when it arrives, are skipped.
  - **Other clients:** they present it when the server's copy arrives.
  - **Requirements:** the script must be networked (any net member) on a `NetIdentity`.
  - **Not yet:** a predicted event the server never produced isn't cancelled, and there's no per-frame FX budget or queue (the GDC review's deferred-FX idea).
  - **Tested by:** NetProbe's dash: "dash fx once" on the owner and "server dash fx #1" on the server, in RunNetSmoke and RunChecks. With `RunNetSmoke.py --observer`, a second, non-owning client logs "observer dash fx once". The owner waits for it before dashing (`Game.NetProbePeers` = 2 through `--user-dir`). Under heavy load the owner still showed the event once through 6 real corrections.
- Keep `[Predicted]` state deterministic: the same code on the same commands. It is compared byte for byte.
- One `ScriptComponent` per networked entity carries its net state.

### Lag compensation (NET-8)

- **View tick:** each client command carries `ViewTick`, the render tick of its interpolated view when the command was made. It goes on the wire as how far behind the command's tick it was, in 1/64 ticks.
- **Aim (NET-15, protocol 6):** a command can carry the player's absolute aim, `HasAim` + `Yaw` (degrees about +Y; 0 looks down +Z, 90 down +X; wrapped to [0, 360)) + `Pitch` (up from level, clamped to ±90). Each is 16 bits (yaw to 0.0055°), sent only when it changes: a held aim costs two bits a command.
  - **Why absolute:** the server aims a shot exactly where the client's view did, and a lost command can't drift it, unlike mouse deltas clamped into the ±1 axes.
  - **Sampling:** C#'s `Net.SetAim( yaw, pitch )` (NetLaunch, until `Net.ClearAim`) puts it in every command, quantized as the server will read it, so the client predicts with the same value.
  - **Reading it:** `Net.Command( e ).Yaw` / `Pitch` / `AimDirection` on both sides. Aim is input, not state: a pawn script that turns or shoots from it in `OnFixedUpdate` predicts it with nothing to correct. Remote players see the turn through the replicated pose.
- **Recording:** after each server tick, `NetLagCompensation::Record` stores every `NetHitbox`'s world pose: a capsule along local Y (Height, Radius) or a box (HalfExtents), around a local Center.
- **The shot:** a server command handler calls `NetCore::LagCompensatedRaycast( origin, direction, range, command, shooter )`.
  1. The hitboxes are interpolated to the command's view tick.
  2. The ray is tested analytically against each, skipping the shooter and everything under it (a pawn's own head hitbox). Nothing in the physics world moves.
  3. A present-day `RaycastAll` finds occluders, ignoring entities with hitboxes, whose bodies are in the present, and the shooter's own colliders and its children's (its gun).
  4. A hit behind an occluder comes back `Occluded`.
- **The cap (NET-15 high ping):** a view older than `MaxRewindSeconds` (0.4 s, about 300 ms RTT plus the view's delay and slack) is judged at the cap (`Clamped`) rather than refused. A target isn't pulled back behind cover further than that, and a shot that misses there simply misses. Only a view tick in the future is `Rejected`.
  - **The client side:** NetCore tells the replication client the oldest render tick the server will still accept (`SetMinRenderTick`: the next command's tick less the cap, less two ticks).
  - **Extrapolation:** when the interpolated view would be older than that, the render tick moves forward, up to `MaxExtrapolationSeconds` (0.25 s) past the newest sample. Remote movers then dead-reckon from their last two samples (`IsExtrapolating`). The view tick sent with commands stays what was really shown.
  - **The 600 ms doctest:** no shot rejected, 71 of 75 judged at the cap, the shown target within about 0.2 m of the server's at the view tick.
  - **Impact FX:** C#'s `Net.PredictImpacts` is false on a client whose round trip is over `Net.PredictImpactsMaxRttMs` (220 ms): draw impacts from the server's confirmation there instead of the predicted hit.
- **Broadphase (NET-15):** `Record` keeps each hitbox's bounds swept over the rewind window. A shot only rewinds and tests the hitboxes whose swept box its ray crosses (`LagCompensatedHit::Tested`), with the same results as testing all of them. In a 200-hitbox doctest that's about 7 tested per shot. `UseBroadphase = false` turns it off for comparisons.
- **Moving occluders (NET-15):** a `NetHitbox` with `Occluder` on (a door, a platform, the payload; give it a shape covering the collider) is recorded like any hitbox. It blocks a shot where it was at the shooter's view tick, never takes the hit, and the shot ignores its present-day colliders. A door that closed after the shooter saw it open doesn't block, and one that was closed in their view does.
- **From C# (NET-15):** `Net.LagCompensatedRaycast( shooter, origin, direction, range )` returns a `NetHit` (`Hit`, `Entity`, `Point`, `Occluded`, `Rejected`, `Clamped`, `Rewound`).
  - **On the server:** it is `LagCompensatedRaycast` with the shooter's current command, so `Rewound` is set. A shooter without a command is judged at the newest recorded tick.
  - **On a client, or offline:** it is `NetCore::PresentRaycast`, the hitboxes where that peer shows them now. That is the predicted hit, for impact FX; only the server's counts.
  - **The shooter's own parts** never stop its shots on any of these paths: a hitbox or collider on the shooter or any entity under it is skipped, so a pawn's child "Head" hitbox doesn't block a shot from its eye.
  - **Also exposed:** `Net.Command(e).ViewTick`, `Net.RoundTripMs` (a client's) and `Net.RoundTripMsTo(clientId)` (the server's, per client).
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
- **Scripts of a shared executable:** the headless Player running a C#-only project builds its scripts into `<project>/.tmp/Scripts/Player-Server-<Config>/` (and `--artifacts-path .tmp/dotnet-Server`), apart from the client Players' `Player-<Config>/`, so a server and its clients on one machine never `dotnet build` into the same folder.
- **Hosts by default:** a headless build with neither `--server` nor `--connect` hosts on the default port (`NetLaunch::IsHosting`), so a shipped server starts with no arguments (ME-308: Build Game's `Server` profile ships it as `<Product>-Server`, `Building.md`). Ctrl+C or SIGTERM stops it through the engine's quit path (`Engine/QuitSignals.h`: the .NET runtime's own SIGTERM handling exited the process mid-frame and crashed the shutdown), so its clients get a Disconnect.
- **Window:** `Engine::Init` creates a `NullWindow` instead of an SDL window.
- **Renderer:** runs bgfx's **Noop** backend (`RendererCreationSettings::Type`). Noop loads the platform's own shader binaries (`GetPlatformString`: spirv / metal / dx11) and draws nothing.
  - Frames only advance bgfx: no mesh/sprite command prep, particles, culling or passes (ME-286). LostTemple averages 6.0 ms a frame instead of 9.9, Playground 0.26 instead of 0.76.
  - Cameras and terrain still update, since terrain streams its colliders.
- **Pace:** with no display to wait on, a headless run caps itself at one frame per tick (`1 / FixedTimeStep`) unless `--max-fps` or the config's `MaxFrameRate` says otherwise.
- **UI:** RmlUi gets no renderer, so it never builds a context: no documents load. Values set through `UI.Model` are kept, unread.
- **Audio:** none. FMOD compiles out (`ME_FMOD` needs `!ME_HEADLESS`) and isn't linked or shipped (`CommonTarget.HasAudio`, ME-287): a dedicated server needs no `libfmod`.
- **Where it builds:** Win64, macOS and Linux, never UWP.

**The command line** (`NetLaunch`, any game build):

| Option | Effect |
|--------|--------|
| `--server [port]` | Host on that UDP port (7777 by default; 0 picks a free one and logs it), every interface, at the engine's tick rate (`1 / FixedTimeStep`). A headless Server build hosts without it, unless it connects |
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
  - The clients start one at a time: each waits until the one before it is on the network, so they never build the scripts they share at once.
- **Running it:**
  - `Engine/Tools/RunNetLocal.py [--scene X.lvl] [--clients N] [--port 7777|0] [--net-args "--net-lag 50 --net-loss 2%"] [--build]`.
  - `--server-args` go to the server only. `--client-args` go to every client, with `{i}` replaced by the client's number from 1 (e.g. `--client-args "--name P{i}"`).
  - `--wrapper Engine/Tools/Headless.sh` runs the clients on virtual displays, each on its own `HEADLESS_DISPLAY` (`:99`, `:100`, …).
  - Outside the dev shell it re-runs itself through `Tools/NixEnv.sh`. Logs and reports land in `.tmp/NetLocal/`.
- **C#-only projects** (`Project/Game.meproj` and no C++; see `Docs/Projects.md`):
  - Run it with `--project <folder>`, or from the project's folder. Server and clients get `--project <folder>`.
  - They run on the Players of the engine checkout the script belongs to: `.build/Server_linux_Debug/Player` (the headless server) and `.build/Game_linux_Debug/Player`. That checkout is the game around the engine, or a standalone engine clone itself: the nearest folder with a `Player` target in its makefile.
  - The scene defaults to the project's `StartupScene`. `--build` builds `Player server_linux_debug` and `Player game_linux_debug` with `Tools/BuildProject.sh --project <checkout>`.
  - Logs and reports land in the project's `.tmp/NetLocal/`.
- **VS Code** (the game's and the project template's `.vscode`):
  - Tasks "Net: server + 2 clients" and "Net: server + 2 clients (50 ms, 2% loss)" build both configurations first (`build-net-debug`) and ask for the scene.
  - Run and Debug has the same thing: "Net: server + 2 clients (no debugger)" runs `RunNetLocal.py` in a terminal.
  - The compounds "Net: server + client (debug both)" and "Net: server + 2 clients (debug all)" run the server and one or two clients under gdb together. They use `--net-timeout 60`, so breakpoints don't drop the session.
- **Window placement:** tiling window managers and native Wayland place windows themselves; the offsets matter on stacking desktops.

**Script hot reload pauses during a session** (ME-311, tools builds):

- **Why:** every tools process watches `Assets/` and rebuilds its C# when a `.cs` changes. With a server and clients on one machine, one edit started several `dotnet build`s at once (the clients share an output folder), and the reload changed the script schema the handshake checks: later joiners were turned away (`scripts`) while the peers already in played on with the old scripts.
- **What happens:** while `NetCore::IsSessionLive()` (hosting until `Close`, or a client connecting or connected), `Engine::PollAssetChanges` doesn't call `ScriptEngine::RequestReload` for a `.cs` change, and a build already running when the session began isn't swapped in (`PollReload( false )` keeps a successful build waiting and logs `Scripts: built; the reload waits for the network session to end`; a failed one is reported as usual). `ScriptReloadGate` (`Source/Engine/ScriptReloadGate.h`) decides, and logs once each: `Scripts: hot reload paused for the network session (edits rebuild once it ends)`, the first held edit, and `Scripts: the network session ended; hot reload resumes` (with `, rebuilding the scripts edited during it` when one was held).
- **When it ends:** a client whose session is over (it left, was rejected, timed out or was disconnected) or a server after `Close` resumes, and rebuilds once if anything changed meanwhile. To pick up an edit, restart the session (RunNetLocal again).
- **Builds take turns:** `ScriptEngine::BuildScripts` holds `.tmp/ScriptBuild.lock` (an advisory `flock` / `LockFileEx`, released on exit or crash, not inherited by dotnet) for the whole `dotnet build`, so a project's processes build one at a time: they share `.tmp/MitchEngine.props` and the dotnet artifacts folder, and sibling clients one output folder. Clients whose session ends together (the server went away) both resume and rebuild; the second logs `Scripts: waiting for another process's script build`, then `another process built them meanwhile; not building again`, and reloads the first's DLL. The same goes for clients started together on stale scripts.
- **A peer started after a held edit** isn't paused yet: tools builds rebuild stale scripts at startup (`ScriptEngine::NeedsBuild`), so a client launched mid-session after a `.cs` edit loads the new scripts and the paused server turns it away (`scripts`) until the session restarts. It can't know which scripts the server has (a fresh server built them too), so this is by design; restart the session after editing scripts.
- **Unchanged:** without a session (the editor, offline play) every edit reloads as before, and the inspector's **Rebuild && Reload Scripts** button always works: it's an explicit request (`RequestReload( true )`), so its build swaps in through the hold.

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
- **Commands (`NetSessionTests.cpp`):** delta-coded lists round-trip, including a resync's tick gap. The server buffer serves, repeats, and ignores late copies. Aim (NET-15) quantizes within 0.01° (yaw wraps, pitch clamps), round-trips through a turn, a hold and a dropped-then-resumed aim, and a held aim costs two bits a command plus its 32 bits once.
- **Capped repeats (`NetSessionTests.cpp`):**
  - **Short gap:** up to the cap (15 misses at 60 Hz), every miss is the last command exactly, its tick moved on. A command in time resets the run, and the next gap gets the whole allowance again.
  - **Long gap:** 600 missed ticks (the 10 s timeout's worth) hold move and fire for 15, then all 585 after are neutral: no buttons, centred axes, the yaw, pitch and view tick kept. A cap of 0 is neutral from the first miss.
  - **Recovery:** the client's late commands for the gap, still holding move and fire, recover only a real tap (1 press). The next tick is the client's command plus that tap, and a miss right after repeats it whole again.
  - **The server's cap** follows its tick rate (15 at 60 Hz, 8 at 30, 32 at 128, at least 1). `SetMaxCommandRepeats` reaches a connected client and a later joiner.
  - **Two worlds:** a client holding move and fire goes silent for 1.5 s. The server held them for 19 ticks (the 15-tick cap plus the few commands already buffered), then let go for 71, aim kept. The client saw 1 stall and held its clock: no resyncs, and no misses once its commands got through again. (Before the stall check it resynced ahead while nothing arrived and took 13 resyncs and 6.5 s to settle, 324 misses in all.) The server ran the client's held command again, with no recovered presses and no timeout.
- **Clock:** dilates towards the target, caps at 5%, resyncs when 13 ticks off, and ignores the next stale report. A stall zeroes the dilation and moves neither the tick, the target nor the resync count. After `QuietTicks` without a report the dilation goes, and quiet ticks don't shrink the target. Behind: a smoothed slack of −2 whose newest command arrived on time only dilates; −3 with the newest −2 jumps 5 ticks (to the target of 3, by the lesser); misses in the 30-tick cooldown leave the target alone, and grow it after; a slack ahead never counts as behind.
- **The server's buffer (`NetSessionTests.cpp`):** ticks before the client's first command are idle, not misses. When the latest packet ends on tick 20 with 40 buffered, the buffer rewinds to 20 (1 rewind) and the new commands for 14–20 replace the old; a reordered packet from before adds nothing past 20, and one from below the newest fills its gap. Late commands move the newest tick: three late by 8–10 ticks read as a slack of −8.
- **Outages (two worlds), the outage done-when:** after 12 s on the link, it's cut for 0.3, 1.5, 3, 5 or 9 s up (client to server; 9 s is just under the timeout), 0.3, 1.5 or 3 s down, or 0.3, 1.5 or 3 s both ways, then restored for 10 s. On 20 ± 2 ms each way and on 50 ± 10 ms with 5% loss, all 22 runs: **0 resyncs**, back within a tick of the target 0.017–0.1 s after the return (≤ 0.25 s required), **0 misses** past the outage's own (the ticks until the first commands after it could arrive), no command run older than before the cut, and the target never deeper than before, at any point from the cut on. A cut that reaches the uplink is 1 stall and 1 outage; a down-only cut is neither. Before the stall check, on 20 ± 2 ms:

  | Cut | Resyncs | Settled after the return | Misses past the outage's own | Oldest command run |
  |-----|---------|--------------------------|------------------------------|--------------------|
  | up 0.3 s | 1 → 0 | 1.98 s → 0.017 s | 2 → 0 | 243 → 43 ms |
  | up 1.5 s | 13 → 0 | 7.47 s → 0.017 s | 162 → 0 | 2043 → 43 ms |
  | up 3 s | 26 → 0 | over 10 s → 0.017 s | 597 → 0 | 93 → 43 ms |
  | down 0.3 / 1.5 / 3 s | 0 → 0 | 0, 0, 0.78 s → 0 | 0 → 0 | 93 → 43 ms |
  | both 0.3 s | 2 → 0 | 0.78 s → 0.017 s | 20 → 0 | 427 → 43 ms |
  | both 1.5 s | 7 → 0 | 3.62 s → 0.017 s | 122 → 0 | 1293 → 43 ms |
  | both 3 s | 7 → 0 | 3.62 s → 0.017 s | 122 → 0 | 1293 → 43 ms |

  On 50 ± 10 ms with 5% loss it was 2–16 resyncs and 0.9–10 s for every cut reaching the uplink, up to 595 extra misses, and commands run up to 2.3 s stale. (The steady state's oldest command was also 110 ms rather than 60: the startup "misses" for ticks before the first command had pushed the target to 6.) **Up 5 s and 9 s** locked the client out for good before the server bounded commands by its own tick rather than the newest it had heard (every command after the cut was "too far ahead", the newest never moved again): every tick a miss, 597 in the 10 s after.
- **Outages at other rates:** up and both-ways 1.5 s cuts at 20 and 30 fps, and 0.3 s up, 2.5 s up and 1.5 s both ways at 128 Hz (a 60 fps client), on both links: 0 resyncs, 1 outage, nothing run older than before. On the clean link, back within a tick at once, 0 misses after and the target never deeper; on the lossy one, none in the first second back and about as many as before the cut after (a lost packet is a gap of a few ticks at these rates), and the target no deeper through that first second. Those later misses grow it as they would have without the cut: over 40 seeds, the target went past where it was in the 10 s after the return in 5–12 of 40 runs at 20 and 30 fps (5–11 before the behind check), so only the clean link checks the whole 10 s.
- **A second client** on its own uplink through the first one's 1.5 s and 5 s uplink cuts: 0 resyncs, 0 stalls, the target unchanged, 0 misses (one in ten seconds on the lossy link, as before the cut).
- **Short hitches (ME-313):** the client runs no frames for 0.05, 0.1, 0.15 or 0.17 s, catching up on every step it missed (and for 0.05–0.15 s on 8, as the engine does: the same, it's all of them): 0 resyncs, 0 outages, 1 stall (none for 0.05 s, inside the usual wait), 1 hitch whose own misses are every miss (2, 5, 8 and 9), back within a tick of the target 0.017 s after at most, 0 misses after, nothing run older than before, and the target never above 1 (where it was). For 0.16 and 0.17 s on 8 steps (it comes back under a tick behind): 1 resync forward, 2 and 1 late commands, none after 0.1 s, the target never above 1, within a tick of the target 0.17 s after (before the behind-at-all jump: no resync, 4 late misses, one after 0.1 s, the target to 7). Before, the target went to 4, 6, 8 and 8 (to 2 for 0.05 s with the misses left out, by the jitter floor: the round trip sample it took, above), commands ran up to 160 ms old against 60, and it took 1–2.5 s to settle.
- **Hitches inside a frame, back to back, on a lossy link (ME-313):** the frame makes its ticks on time and sends only after a 0.05, 0.1 or 0.14 s stall (`NetHarness::ClientHitchInFrame`; 0.1 s also on 8 steps, and 0.05 s on 50 ms each way): 0 resyncs, 0 outages, every miss the hitch's own (2, 5, 8), the target never above 1. Past the engine's catch-up (0.17 s on 8 steps) 1 resync forward, and 0.25 s an outage too, the target never above 1. Two hitches a frame apart on 50 ms each way (0.05 s twice, 0.1 then 0.05 s, and 0.05 s twice inside frames): 2 hitches, every miss theirs (4, 7, 4), the target never above 1. On 50 ± 10 ms with 5% loss (0.05, 0.1, 0.15 s, and 0.1 s inside a frame): 0 resyncs, 0 outages, the target never deeper than before. Before: an in-frame 0.1 s hitch took the target from 1 to 8 (1 of its 5 misses counted its own: the burst was only the late frame's tick, and the jitter floor rose on the samples read late), 0.14 s resynced on the report on that tick and was an outage; 0.1 then 0.05 s on 50 ms took it to 6 (2 of 7 its own).
- **Hitches:** the client runs no frames for 0.3 or 1 s, catching up on every step it missed: 0 resyncs (before: 1 and 8). For 0.18, 0.2, 0.25, 0.3 or 1 s, catching up on 8 and dropping the rest, as the engine does, so it comes back about 3, 4, 7, 10 or 52 ticks behind: exactly 1 resync, filled, then 1 late command past the hitch's own misses and none after 0.1 s (≤ 2 and 0 required). Each is 1 stall and 1 outage, the target never above 1 (where it was), nothing run older than before. Back within a tick of the target 0.067 s after, or 0.24 s after the 0.18 s hitch (≤ 0.25 s required): its jump is under 4 ticks, which the server's smoothed slack follows over about 10 ticks. (Snapping only past 4 ticks, the 0.2 s hitch's jump of exactly 4 took 0.28 s, and the bound was 0.35 s; ME-314.) Before the behind check those three missed 8, 7 and 4 times past their own, took the target to 7, 6 and 3 (5.5, 4.5 and 1.5 ten seconds later) and ran commands up to 160 ms old against 60; before the stall check the 0.3 and 1 s hitches took 1 and 3 resyncs and the 1 s one 45 misses.
- **Server hitches:** the server stops for 0.15, 0.3, 0.5, 1, 1.5 or 3 s, then catches up taking 0, 3 or 8 ms a tick, either every tick due at once or 8 a frame, a frame a tick, as `Engine::Run` runs a headless server (`NetHarness::ServerStepsPerFrame`). Up to 0.5 s: 0 resyncs, 0 misses, 0 rewinds, the target unchanged, and the slack within a tick of the target from the catch-up's first tick on (≤ 0.25 s after the hitch required). Longer hitches lose the ticks past 0.5 s (30, 60 and 150), as `Engine::Run` does: exactly 1 resync, back, 1 rewind, 0 misses, nothing run that the client took back, within a tick of the target 0.012 s after the hitch at most (≤ 1 s required), the target never deeper. (Measuring against the step it was on, a 0.3 s hitch caught up at 3 ms a tick cost 2 resyncs, a rewind and 44 misses; the 1 s one at 8 ms, 3 resyncs and 107. With the slack past ±64 ticks read from the newest and the tick, the 3 s hitch cost 2 resyncs, 50–57 misses, up to 2 commands run that the client took back, and the target went to 8.)
- **A server that can't keep up:** its ticks take 20 ms for 10 s (50 a second). It drops its backlog (100 ticks in all), and its client follows it: 10 resyncs back, the oldest command run 250 ms against 60 before (≤ the steady state's plus `ResyncTicks` + 2 ticks), nothing run the client took back.
- **Latency jumps:** the uplink goes from 200 to 20 ms: 1 resync back, 1 rewind, 0 misses, and nothing run older than the 200 ms link had made it; with a quarter of the packets held back 30 ms more, so that packets from before the jump keep arriving after ones from after it, the same, with 0 commands run that the client took back (3 seeds). From 20 to 200 ms: 1 stall while nothing arrives, then 1 resync forward, 24 misses (the 180 ms gap and a round trip of late commands), within a tick of the target 1.5 s after (before: 2.9 s; the jitter floor the step inflates keeps rising for a sample after the jump, and the clock dilates the last ticks).
- **A jump back shorter than the round trip:** the uplink drops from 400 to 100 ms (18 ticks against a 25-tick round trip), then loses a quarter of its packets: 1 resync, 1 rewind, 0 misses, nothing run the client took back (3 seeds). Reports from before the server heard the jump confirm nothing, so the commands made again keep riding every packet.
- **A paused client:** paused for 30 minutes on keep-alives (36000 packets, past half the sequence range), it's heard at once when it resumes: 1 resync forward (by the slack in whole ticks, beyond ±64, and the slack its newest command arrived with), then 300 commands on time and 0 missed in 5 s.
- **A late start:** the uplink slows by 150 ms as the server accepts the client, so all its first commands arrive late: 1 resync and from then on time (575 commands), no misses after 2 s. Before, the server's buffer stayed empty and the client was never told: 0 on time out of 9272 for good.
- **Frame rates:** at 20, 30, 60, 144 and 240 fps, and at 60 fps against a 128 Hz server, on 20 ± 2 ms and on 50 ± 10 ms with 5% loss, a minute each: no resyncs, no outages, and no stalled reports on the clean link (on the lossy one at most 4, two packets in a row lost at 20 fps). Before, 20 and 30 fps with loss resynced once each, and at 60, 144 and 240 fps with loss the client started behind and the server never ran one of its commands.
- **Latency spikes (Wi-Fi background scans):** every 4 s for 12 spikes, the radio goes away and what was sent meanwhile arrives at once. 100 ms up on 20 ± 2 ms: 5 misses on the first spike, then 0 or 1 each (2 at most; 6 in the last 10); both ways, the same (5, then 0–2, 5 in the last 10); 50 ms: 2, then 0 or 1; 150 ms (past `MaxSlack`'s 133 ms): 8, then 1 each; 100 ms on 40 ± 5 ms with 2% loss, up or both ways: 2–3, then 0–2. Never a resync or an outage, and never read as the client's own hitch (the outage runs check that too). With the client estimating the server's wait from its own send times, the both-ways spike read the server's held reports as fresh and resynced; with each gap's misses thrown out as a stall's, every spike cost about 4 misses for good.
- **Two worlds, the NET-2 done-when:** 60 s at 50 ± 10 ms each way (100 ± 20 ms RTT) with 5% loss. 3599 of 3600 commands arrived on time, ≥ 99.9% required. Worst slack error 0.24 tick (≤ 1; 0.90 before the stall check), worst dilation 3.2% (≤ 5%; 4.3% before), no resyncs, no stalls. The server simulated exactly the client's commands. After the link slows by 60 ms each way, the slack is back within 1 tick in 10 s.
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
  - **Teleports:** a 20 cm correction glides (an offset under 2 m); one that moves the pawn 25 m (a respawn) snaps: offset zero at once and after the next frame.
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
    - all 60 forged views 30 ticks further back were rejected (NET-15: now judged at the cap instead).
  - **Aim in the command (NET-15), at 150 ms RTT:** the client turns its view onto the strafing target where it shows it, sending absolute yaw and pitch. The server shoots along each command's aim: 60 of 60 shots hit, with the server's aim equal to the client's to the bit.
  - **Present view (NET-15):** a client's `PresentRaycast` hits its hitbox where it shows it, with no rewind, and misses 2 m to the side. At 50 ms each way, both ends' `GetRoundTripMs` read about 117 ms.
  - **The shooter's own parts:** a pawn with a body hitbox, a child head hitbox and a grandchild gun collider shoots from inside its head, through its gun: `PresentRaycast` and the rewound `LagCompensatedRaycast` both hit the target 10 m ahead, unoccluded. Anyone else's ray from there stops at the head, and the gun still blocks other shots.
- **Handshake:** another build is rejected (`"version mismatch: build"`) and the server lets it go; so is another scene (`"version mismatch: scene"`); a match connects.
- **Script hot reload pauses for a session (`ScriptReloadGateTests.cpp`, ME-311):** the gate holds every edit while live and asks for one rebuild when the session ends (none if nothing changed); without a session each edit reloads. Two worlds: server and client are live from `Host` / `Connect` (before the handshake finishes) and while connected; the client's `Close` ends its session (still `IsActive`) but not the server's; the server's `Close` ends its; each then rebuilds the edits it held, and hosting again is live again. A client rejected for another script schema isn't live, so its reload resumes. Headless, on a copy of HeroShooter (2026-11-06): the Server Player hosting logged the pause and held two `.cs` edits (no build); a client connecting to a port nobody answers held them, then on its timeout logged the resume and rebuilt once; the game Player offline rebuilt and hot reloaded each edit. Two clients connecting to a dead port held one edit each and timed out together: one rebuilt, the other waited on the build lock, skipped its build and hot reloaded the same DLL (and at their start, on stale scripts, the same). MatchSmoke checks that the server and each client logged the pause.
- **UDP and session endings (`NetUdpTests.cpp`):**
  - Addresses resolve (`localhost` too) and print, and malformed ones are refused.
  - Two sockets on 127.0.0.1 trade datagrams up to 1200 bytes, with each `From` equal to the sender's resolved address, and a taken port fails to bind.
  - A `NetServer` and `NetClient` connect over real UDP. Commands arrive, and the client's Disconnect removes it at once (1 disconnect, 0 timeouts).
  - A client drawing at 1000 fps against a 60 Hz server sends 60.3 packets/s, with a loss estimate of 0. Before the cap it sent one packet a frame; past the server's 33-packet ack window those read as 34% loss on a desktop run.
  - On loopback, a silent client times out on the server (`OnClientRemoved` fires once). A client gives up on a silent server and on one that never answers. A closing server tells its client (`"server closed"`), and a leaving client is counted as left, not timed out.
  - **Session events** (`NetSessionEvent` listeners, one per world, plus a second on the server's to show both hear each event once): the server hears `ClientJoined` for each of two clients, `ClientLeft` `"left"` when one says goodbye and `"closed"` for the other when it shuts down; a mute client is `"timed out"`. Clients hear `Connected` with their id, then `Disconnected` `"left"`, `"server closed"` or `"timed out"`, and a turned-away one only `Disconnected` `"rejected: version mismatch: build"`. A listener on one world never hears another's. A server whose goodbye has bytes that aren't UTF-8 (`"bye \xFF\xC0\xAF!"`) reaches the client as `"bye ���!"`, which dumps as JSON (`NetTests.cpp` checks `ToValidUtf8` on overlong, surrogate, out-of-range and cut-short sequences).
- **Command layout and the local command (`InputTests.cpp`):** networked buttons take bits by name (Ability 0, Fire 1, Jump 2) and Value / Vector2 actions axes after (Move 0-1, Throttle 2); non-networked actions have none. Sampled each step, Jump is pressed then held (`PreviousButtons`), the axes are quantized as the server reads them, the aim rides along, and a disabled context sends nothing. The layout is rebuilt only when the map changes. `ResetLocalInput` (the editor's Stop) drops a held `Press`, the aim and the recorded command but keeps the layout, and `ClearLocalCommand` leaves an aim-only command invalid once the aim is cleared. C# (`Generators.Tests/NetSessionEventTests.cs`): the four session events dispatch typed from the engine's JSON, and without an engine `LocalCommand`, `ButtonIndex` / `AxisIndex`, `NetCommand.Vector2` / `Value`, `VisualOffset` and `CommandLine` read empty.
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
    - **NET-15:** the dash also fires a shot at "NetProbe Target", a `NetHitbox` 3 m off to the side, along the aim the owner sends with `Net.SetAim`. The server logs **"lag-compensated hit (aim yaw 26.565"** (judged about 10 ticks back at the owner's view). The owner logs **"predicted hit (aim yaw 26.565"** from its present-view ray, with its RTT and **"predict impacts True"**.
- **The Player as a dedicated server:** RunNetSmoke with `--server-game .build/Server_linux_Debug/Player --client-game .build/Game_linux_Debug/Player` passes the same NetTest expectations as the game's own builds (every NetProbe line, 60.0 snapshots/s, the same capture), so a C#-only project needs no game executable to host. A C#-only project run as a headless server Player and two client Players logged `NetClientJoined` 1 and 2 and `NetClientLeft 1 (left)` on the server, `NetConnected` on each client and `NetDisconnected (server closed)` on the one still connected when the server quit, with the server's scripts in `Player-Server-Debug/`.
- **The NET-5 done-when (`RunNetSmoke.py`):** the Server build and a client on NetTest.lvl over localhost UDP for 900 client frames (about 15 s).
  - **Clean link:** connected, 0 timeouts, 0 resyncs, 60.06 snapshots/s at 60 Hz (±5% required). The client left cleanly and the server exited by itself. The capture matches its reference exactly (0.00%).
  - **Lossy link:** with `--net-lag 40 --net-jitter 5 --net-loss 2%` on both sides: RTT 111 ms, 59.08 snapshots/s, 0 timeouts, the same capture.
  - **With the stall check and catch-up slack:** both again with 0 resyncs, 0 stalls and 0 missed commands (RTT 15 and 115 ms). The observer run (two clients, the server loading the second's world as it joins) passes too.
  - **Protocol 7 (the server's wait, gaps and outages, the catch-up bound):** clean 0 resyncs, 0 stalls, 0 outages, 0 missed (RTT 15.5 ms); lossy the same (RTT 115 ms); the observer run 0 resyncs, 1 stall and 4 late commands as the second client joined. HeroShooter's `Tools/MatchSmoke.py --expect-match-over` (a dedicated server Player and two autopilot clients, a full match to 3): 10 kills, 0 resyncs and 0 stalls on both clients, RTT 18 and 32 ms, 29 and 20 corrections against about 6500 matches; with `--net-lag 40 --net-jitter 5 --net-loss 2%`, 10 kills, 0 resyncs, 1 stall on one client, RTT 99 and 102 ms, 23 and 15 corrections against about 6400 matches.
- **Round trip:** at 50 ms each way, 5% loss, with 0 or ±10 ms jitter, both ends estimate within 10% of 100 ms. Garbage and lying packets are rejected, and oversized messages are refused. An end at 5 fps on a 50 ms link keeps sampling: a round trip of 186 ms (its frames' wait included), deviation 2.3 ms (a fixed 100 ms cutoff froze it at half the first sample). After 40000 packets, the next is still the newest; a reordered one isn't.

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
- **Hot reload during a session** is paused (ME-311, above). Forced from the inspector's button, it changes the schema hash for new joiners only, and drops the generated state of reloaded instances back to their saved public fields. A peer launched after a held edit builds the new scripts at startup and is turned away until the session restarts.
- **Spawned prefabs:** clients create spawned entities from `NetIdentity::Prefab`, which must exist on the client.
- **UDP is IPv4 only** (a `NetAddress` is 64 bits).
  - On a multi-homed server bound to every interface, a reply can leave from another address than the one the client dialled, and the client then ignores it. Bind one address, or connect to the address it answers from.
  - There is no NAT traversal; Steam SDR (NET-9) brings that.
- **Ticks follow the engine's fixed steps.** Don't give a server `--frame-time`: its ticks then follow frames rather than the clock, so any stall or speed-up shows up as client resyncs. Real-time servers step by the clock and catch up (`KeepsBacklog`), measuring slack against the caught-up tick. A hitch longer than `NetServer::kMaxCatchUpSeconds` (0.5 s) loses ticks for good, which clients see as one resync back. A server too slow to keep up keeps no backlog, and its clients resync back every second or so (each time they get 8 ticks ahead); that's the price of their input not waiting on a backlog.
- **The harness mirrors `Engine::Run`'s catch-up** (the 0.5 s bound, dropping a backlog it can't run, and with `ServerStepsPerFrame` 8 steps a frame, a frame a tick), but reads packets before every step rather than once a frame. Change both together.
- **Mixed builds:** protocol 7 changed what reports mean, not just their bytes. The build hash folds the protocol in, so a protocol-6 client or server is rejected (`"version mismatch: protocol build"`) rather than misread.
- **The jitter floor follows the round trip's deviation,** which a step in latency inflates for a moment (so does the first sample: the deviation starts at half the round trip, and a two-way latency spike). The target it raises eases down at a quarter tick a second, so after a big latency change (or on a high-latency link's first seconds) the buffer is deeper than the link needs for a while.
- **Own hitches buy no buffer.** A client hitch's misses on the ticks it made late never grow the target (ME-313), so a client that hitches often (a pause every second or two) misses on every one of them rather than buffering deeper. Hitch detection is by send time: a frame two ticks past the usual counts, so at 20 fps a 0.1 s frame is a hitch and a 0.07 s one isn't.
- **Two-frame hitches aren't told apart.** At 60 fps a hitch under about 0.045 s looks like the frame-to-frame wobble dilation causes (a frame that runs no tick, then one that runs two): its miss grows the target, and its round-trip sample passes too, so on a 20 ms link a 0.03–0.04 s hitch takes the target from 1 to 3 for a few seconds. One tick past the usual instead of two read dilation's empty frames as hitches.
- **A hitch the engine drops time on costs a resync.** Past 8 steps and a frame (0.155 s at 60 Hz, by the frame's phase) the clock comes back behind and jumps forward (**Behind**, **Back from its own hitch**): filled, and the target unchanged, but the ticks dropped are skipped rather than simulated, and the server's smoothed slack takes up to a quarter second to follow a jump under 4 ticks. On a link whose target is a few ticks deep (jitter, loss), the slack absorbs the drop instead.
- **Spikes and the shrink:** spikes further apart than `ShrinkAfterSeconds` (3 s) let the target ease a little between them, so a spike every few seconds still costs a miss now and then (0–1 a 100 ms spike once the buffer has grown). Spikes longer than `MaxSlack` (8 ticks: 133 ms at 60 Hz) cost what the deepest buffer can't cover, every time; longer than that plus the usual wait, they're outages and don't grow it at all.

## Related Docs

- [Physics.md](Physics.md): the character NetState and SimTick (NET-0).
- [Scripting-DotNet.md](Scripting-DotNet.md): `ScriptBindings::WorldScope`.
- [Architecture.md](Architecture.md): the frame these hooks sit in.
