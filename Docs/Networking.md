# Networking

Multiplayer follows the Overwatch model: an authoritative server, client prediction, and C# first. It is built in steps, NET-0 to NET-11 on the roadmap. What exists so far is the foundation:

- **NET-0:** a deterministic character step and its replayable state (see [Physics.md](Physics.md)). Scripts can act on a scoped world (see [Scripting-DotNet.md](Scripting-DotNet.md)).
- **NET-1:** the wire layer described here. It covers the bit stream, quantization, transports, and a connection with acks, round trip, loss and a reliable ordered stream. It's pure C++ with no engine dependencies beyond the math types.

Sessions, the clock, replication, prediction and the C# API come next (NET-2 onwards).

## Key Files

| Path | Role |
|------|------|
| `Source/Net/BitStream.h` | `BitWriter` / `BitReader`: bits LSB-first into little-endian bytes, VarUInt, raw floats, bytes, strings. Readers never overrun: they return zero and set `IsOverflowed()` |
| `Source/Net/NetQuantize.h` | A float in a range at N bits, round-tripping within half a step (out of range clamps, NaN maps to the minimum). `Vector3` per component; quaternions as "smallest three", 2 + 3·N bits |
| `Source/Net/NetTransport.h` | `INetTransport` (datagrams between `NetAddress`es). `LoopbackNetwork` + `LoopbackTransport` join worlds in one process. `ConditionedTransport` wraps any transport with `NetConditions` |
| `Source/Net/NetConnection.h` | `NetConnection`, one end of a link, and `NetStats` |
| `Tests/Source/NetTests.cpp` | The proofs (below) |

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

## Proofs

`NetTests.cpp`:

- **Bit stream:** round-trips widths of 1–32 bits, VarUInt up to 2⁶⁴−1, float bits, bytes and strings. It flags truncation and refuses a hostile string length.
- **Quantization:** floats at 4, 10, 16 and 24 bits come back within half a step, vectors at 20 bits, and 500 random rotations at 12 bits (|dot| > 0.99999).
- **Reliable under a bad link:** 600 messages one way and 150 the other, over 40 ms ± 15 ms with 20% loss, 10% duplicates and 10% reordering, arrive complete, in order and once. Unreliable messages arrive at most once (most of them). The loss estimate reads 10–32%.
- **Round trip:** at 50 ms each way, 5% loss, with 0 or ±10 ms jitter, both ends estimate within 10% of 100 ms. Garbage and lying packets are rejected, and oversized messages are refused.

## How to Extend

- **A real transport (Steam, UDP):** implement `INetTransport`. `Update` can stay empty, and `NetConnection` doesn't change.
- **Bigger messages:** need fragmentation (a reliable slice stream). Until then, keep replicated state under 1 KB a message.

## Caveats & Fragility

- **Clock:** the connection trusts the clock it's given. Pass the same monotonic `now` to `SendPacket` and `ReceivePacket`.
- **Reliable window:** a stalled link holds at most 1024 messages in flight. Messages queued beyond that wait in memory, unbounded, until acks free the window.
- **No encryption or authentication:** the protocol id only filters strays.

## Related Docs

- [Physics.md](Physics.md): the character NetState and SimTick (NET-0).
- [Scripting-DotNet.md](Scripting-DotNet.md): `ScriptBindings::WorldScope`.
