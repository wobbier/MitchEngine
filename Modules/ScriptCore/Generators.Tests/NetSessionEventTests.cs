using System.Collections.Generic;
using System.Reflection;
using ScriptCore;
using Xunit;

// The engine's network session events arrive as typed events on the EventBus; without an engine
// the command-line, command-layout and local-command calls answer as if nothing were there.
// EventBus is shared with SequenceEventTests: same collection, so they never run at once.
[Collection("ScriptRegistry")]
public class NetSessionEventTests
{
    private static void Dispatch(string json) =>
        typeof(EventBus).GetMethod("DispatchEngine", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, new object[] { json });


    [Fact]
    public void Engine_net_events_publish_joins_leaves_and_how_a_client_s_session_went()
    {
        var joined = new List<NetClientJoined>();
        var left = new List<NetClientLeft>();
        var connected = new List<NetConnected>();
        var disconnected = new List<NetDisconnected>();
        using var a = EventBus.Subscribe<NetClientJoined>(joined.Add);
        using var b = EventBus.Subscribe<NetClientLeft>(left.Add);
        using var c = EventBus.Subscribe<NetConnected>(connected.Add);
        using var d = EventBus.Subscribe<NetDisconnected>(disconnected.Add);
        Dispatch("""
            [ { "Type": "NetClientJoined", "ClientId": 3 },
              { "Type": "NetClientLeft", "ClientId": 3, "Reason": "timed out" },
              { "Type": "NetConnected", "ClientId": 2 },
              { "Type": "NetDisconnected", "Reason": "rejected: version mismatch: build" } ]
            """);
        Assert.Equal(new[] { new NetClientJoined(3) }, joined);
        Assert.Equal(new[] { new NetClientLeft(3, "timed out") }, left);
        Assert.Equal(new[] { new NetConnected(2) }, connected);
        Assert.Equal(new[] { new NetDisconnected("rejected: version mismatch: build") }, disconnected);
    }


    [Fact]
    public void Without_an_engine_there_is_no_local_command_layout_or_command_line()
    {
        Assert.False(Net.LocalCommand.Valid);
        Assert.Equal(-1.0, Net.LocalCommand.ViewTick);
        Assert.Equal(-1, Net.ButtonIndex("Fire"));
        Assert.Equal(-1, Net.AxisIndex("Move"));
        Assert.Equal(default(Vector3), Net.VisualOffset(new Entity(1, 1)));
        var command = new NetCommand { Valid = true, Axis0 = 0.5f, Axis1 = -0.5f };
        Assert.Equal(default(Vector2), command.Vector2("Move"));
        Assert.Equal(0f, command.Value("Throttle"));
        Assert.False(CommandLine.Has("bots"));
        Assert.Equal("Player", CommandLine.Get("--name", "Player"));
        Assert.Equal(4, CommandLine.GetInt("bots", 4));
        Assert.Equal(0.5f, CommandLine.GetFloat("scale", 0.5f));
        Assert.True(Input.IsContextEnabled("Gameplay"));
    }


    [Fact]
    public void Command_line_names_work_with_or_without_their_dashes()
    {
        var flag = typeof(CommandLine).GetMethod("Flag", BindingFlags.NonPublic | BindingFlags.Static)!;
        Assert.Equal("--bots", flag.Invoke(null, new object[] { "bots" }));
        Assert.Equal("--bots", flag.Invoke(null, new object[] { "--bots" }));
        Assert.Equal("-v", flag.Invoke(null, new object[] { "-v" }));
    }
}
