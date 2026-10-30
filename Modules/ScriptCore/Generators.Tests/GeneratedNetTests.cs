using System;
using System.Reflection;
using ScriptCore;
using Xunit;

// NET-6: the INetScript members the real NetGenerator wrote for NetScriptProbe.
public class GeneratedNetTests
{
    // 4 + 12 predicted; + 4 + 1 + 8 + 8 + 16 + 4 replicated.
    private const int PredictedSize = 16;
    private const int ReplicatedSize = 57;

    private static NetScriptProbe MakeProbe() => new NetScriptProbe
    {
        Cooldown = 1.25f,
        Score = 42,
        Velocity = new Vector3(1f, -2f, 3.5f),
        Armed = true,
        Clock = 1234.5678,
        Aim = new Vector2(0.25f, -0.75f),
        Facing = new Quaternion(0.1f, 0.2f, 0.3f, 0.9f),
        Mode = NetProbeMode.Dashing,
        NotNetworked = 5,
    };


    [Fact]
    public void Flags_and_sizes_follow_the_convention()
    {
        INetScript net = MakeProbe();
        Assert.Equal(7, net.NetFlags);
        Assert.Equal(PredictedSize, net.NetSavePredicted(Span<byte>.Empty));
        Assert.Equal(ReplicatedSize, net.NetWriteReplicated(Span<byte>.Empty));

        // Too small: the size comes back and nothing is written.
        var small = new byte[PredictedSize - 1];
        Assert.Equal(PredictedSize, net.NetSavePredicted(small));
        Assert.All(small, b => Assert.Equal(0, b));
        Assert.NotEqual(0UL, NetScriptProbe.__NetSchemaHash);
    }


    [Fact]
    public void Predicted_state_round_trips_bit_exact()
    {
        var probe = MakeProbe();
        INetScript net = probe;
        var saved = new byte[PredictedSize];
        Assert.Equal(PredictedSize, net.NetSavePredicted(saved));
        Assert.Equal(1.25f, BitConverter.ToSingle(saved, 0));

        probe.Cooldown = 0f;
        probe.Velocity = default;
        probe.Score = 7;
        net.NetRestorePredicted(saved);
        Assert.Equal(1.25f, probe.Cooldown);
        Assert.Equal(new Vector3(1f, -2f, 3.5f).x, probe.Velocity.x);
        Assert.Equal(-2f, probe.Velocity.y);
        Assert.Equal(3.5f, probe.Velocity.z);
        Assert.Equal(7, probe.Score);

        // A short buffer leaves the state alone.
        net.NetRestorePredicted(new byte[3]);
        Assert.Equal(1.25f, probe.Cooldown);
    }


    [Fact]
    public void Replicated_state_round_trips_and_can_skip_predicted_fields()
    {
        INetScript source = MakeProbe();
        var written = new byte[ReplicatedSize + 8];
        Assert.Equal(ReplicatedSize, source.NetWriteReplicated(written));

        var full = new NetScriptProbe();
        ((INetScript)full).NetReadReplicated(written.AsSpan(0, ReplicatedSize), skipPredicted: false);
        Assert.Equal(1.25f, full.Cooldown);
        Assert.Equal(42, full.Score);
        Assert.Equal(3.5f, full.Velocity.z);
        Assert.True(full.Armed);
        Assert.Equal(1234.5678, full.Clock);
        Assert.Equal(-0.75f, full.Aim.y);
        Assert.Equal(0.9f, full.Facing.w);
        Assert.Equal(NetProbeMode.Dashing, full.Mode);
        Assert.Equal(0, full.NotNetworked);

        var owner = new NetScriptProbe { Cooldown = 9f, Velocity = new Vector3(9f, 9f, 9f) };
        ((INetScript)owner).NetReadReplicated(written.AsSpan(0, ReplicatedSize), skipPredicted: true);
        Assert.Equal(9f, owner.Cooldown);
        Assert.Equal(9f, owner.Velocity.x);
        Assert.Equal(42, owner.Score);
        Assert.Equal(0.2f, owner.Facing.y);
        Assert.Equal(NetProbeMode.Dashing, owner.Mode);
    }


    [Fact]
    public void Rpcs_dispatch_to_their_handlers()
    {
        var probe = new NetScriptProbe();
        INetScript net = probe;

        // Ids in ordinal name order: Ack 0, Hello 1, Ping 2.
        Span<byte> buffer = stackalloc byte[256];
        var writer = new NetWriter(buffer);
        writer.WriteInt(5);
        writer.WriteString("héllo");
        writer.WriteVector3(new Vector3(1f, 2f, 3f));
        writer.WriteInt((int)NetProbeMode.Dashing);
        writer.WriteBool(true);
        Assert.False(writer.Overflowed);
        net.NetDispatchRpc(1, writer.Written);
        Assert.Equal(5, probe.HelloValue);
        Assert.Equal("héllo", probe.HelloText);
        Assert.Equal(2f, probe.HelloWhere.y);
        Assert.Equal(NetProbeMode.Dashing, probe.HelloMode);
        Assert.True(probe.HelloFlag);

        writer = new NetWriter(buffer);
        writer.WriteFloat(0.5f);
        net.NetDispatchRpc(0, writer.Written);
        Assert.Equal(0.5f, probe.AckValue);

        net.NetDispatchRpc(2, ReadOnlySpan<byte>.Empty);
        Assert.Equal(1, probe.Pings);

        // Truncated arguments and unknown ids are ignored.
        net.NetDispatchRpc(0, new byte[2]);
        Assert.Equal(0.5f, probe.AckValue);
        net.NetDispatchRpc(99, ReadOnlySpan<byte>.Empty);
    }


    [Fact]
    public void Offline_rpc_stubs_run_locally()
    {
        // No session (the engine API isn't bound here): Net.Role is 0, both sides are this one.
        Assert.Equal(0, Net.Role);
        var probe = new NetScriptProbe();
        probe.SendHello(3, "offline", new Vector3(4f, 5f, 6f), NetProbeMode.Idle, true);
        Assert.Equal(3, probe.HelloValue);
        Assert.Equal("offline", probe.HelloText);
        Assert.Equal(6f, probe.HelloWhere.z);
        probe.SendAck(2f);
        Assert.Equal(2f, probe.AckValue);
        probe.Ping();
        Assert.Equal(1, probe.Pings);
    }


    [Fact]
    public void Writer_and_reader_stop_cleanly_at_the_end()
    {
        Span<byte> buffer = stackalloc byte[6];
        var writer = new NetWriter(buffer);
        writer.WriteInt(1);
        writer.WriteFloat(2f);
        Assert.True(writer.Overflowed);
        Assert.Equal(4, writer.Position);
        writer.WriteBool(true);
        Assert.Equal(4, writer.Position);

        var reader = new NetReader(writer.Written);
        Assert.Equal(1, reader.ReadInt());
        Assert.Equal(0f, reader.ReadFloat());
        Assert.True(reader.Overflowed);

        Span<byte> tiny = stackalloc byte[8];
        var strings = new NetWriter(tiny);
        strings.WriteString("too long for eight bytes");
        Assert.True(strings.Overflowed);
        Assert.Equal(0, strings.Position);

        var nulls = new NetWriter(tiny);
        nulls.WriteString(null);
        var nullReader = new NetReader(nulls.Written);
        Assert.Null(nullReader.ReadString());
    }


    [Fact]
    public void State_paths_do_not_allocate_after_warm_up()
    {
        var probe = MakeProbe();
        INetScript net = probe;
        var predicted = new byte[PredictedSize];
        var replicated = new byte[ReplicatedSize];
        Span<byte> args = stackalloc byte[4];
        new NetWriter(args).WriteFloat(1f);

        void Cycle(Span<byte> rpcArgs)
        {
            net.NetSavePredicted(predicted);
            net.NetRestorePredicted(predicted);
            net.NetWriteReplicated(replicated);
            net.NetReadReplicated(replicated, skipPredicted: true);
            net.NetReadReplicated(replicated, skipPredicted: false);
            net.NetDispatchRpc(0, rpcArgs);
            probe.SendAck(1f);
        }

        for (int i = 0; i < 16; ++i) Cycle(args);
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 1000; ++i) Cycle(args);
        long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        Assert.Equal(0, allocated);
    }


    [Fact]
    public void Handshake_hash_covers_the_net_scripts()
    {
        var compute = typeof(ScriptBridge).GetMethod("ComputeNetSchemaHash", BindingFlags.NonPublic | BindingFlags.Static)!;
        ScriptRegistry.Scan(typeof(NetScriptProbe).Assembly);
        try
        {
            var first = (ulong)compute.Invoke(null, null)!;
            var second = (ulong)compute.Invoke(null, null)!;
            Assert.NotEqual(0UL, first);
            Assert.Equal(first, second);

            // FNV-1a 64 over "NetScriptProbe" + the type's hash, by hand.
            ulong expected = 14695981039346656037UL;
            void Mix(byte[] bytes)
            {
                foreach (var b in bytes) { expected ^= b; expected *= 1099511628211UL; }
            }
            Mix(System.Text.Encoding.UTF8.GetBytes("NetScriptProbe"));
            Mix(BitConverter.GetBytes(NetScriptProbe.__NetSchemaHash));
            Assert.Equal(expected, first);
        }
        finally
        {
            ScriptRegistry.Clear();
        }
    }
}
