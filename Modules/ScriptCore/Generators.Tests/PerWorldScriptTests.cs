using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using ScriptCore;
using Xunit;

// ME-343: what C# keeps across scripts is kept per world (Scripts.All, Entity.GetScript) and per net session
// (Net.Events), so a killcam's replay world beside the live one shares none of it: its copies of the live
// world's entities have the same Index / Generation, and its ticks repeat the live world's.
public class PerWorldPawn : Script { public string Name = ""; }

[Collection("ScriptRegistry")]
public unsafe class PerWorldScriptTests
{
    private static int s_world;
    private static int s_session;

    [UnmanagedCallersOnly] private static int WorldId() => s_world;
    [UnmanagedCallersOnly] private static int SessionId() => s_session;

    private static Dictionary<int, IGameScript> Instances =>
        (Dictionary<int, IGameScript>)typeof(ScriptBridge).GetField("Instances", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
    private static List<int> Order =>
        (List<int>)typeof(ScriptBridge).GetField("_handleOrder", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
    private static Dictionary<int, int> Worlds =>
        (Dictionary<int, int>)typeof(ScriptBridge).GetField("_handleWorld", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;

    // The engine's two answers (the script world's id, its session's), as the engine binds them.
    private static void Bind(bool fake)
    {
        var api = new EngineAPIBindings();
        if (fake)
        {
            api.World_GetId = &WorldId;
            api.Net_GetSessionId = &SessionId;
        }
        typeof(Engine).GetMethod("Bind", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, new object[] { api });
    }

    private static PerWorldPawn Pawn(Entity entity, string name)
    {
        var script = new PerWorldPawn { Name = name };
        typeof(Script).GetProperty("Entity")!.SetValue(script, entity);
        return script;
    }

    private static void Add(int handle, int world, PerWorldPawn script)
    {
        Instances[handle] = script;
        Order.Add(handle);
        Worlds[handle] = world;
    }

    [Fact]
    public void Scripts_All_and_GetScript_keep_to_the_caller_s_world()
    {
        Assert.Empty(Instances);
        Bind(true);
        try
        {
            // The live world (1) and a killcam's replay world (2): the same entity ids in each.
            var pawn = new Entity(3, 1);
            var bot = new Entity(4, 1);
            Add(10, 1, Pawn(pawn, "live pawn"));
            Add(11, 1, Pawn(bot, "live bot"));
            Add(12, 2, Pawn(pawn, "replay pawn"));

            s_world = 1;
            Assert.Equal(1, Engine.WorldId);
            Assert.Equal(2, Scripts.Count<PerWorldPawn>());
            Assert.Equal("live pawn", Scripts.First<PerWorldPawn>()!.Name);
            Assert.Equal("live pawn", pawn.GetScript<PerWorldPawn>()!.Name);

            s_world = 2;
            Assert.Equal(1, Scripts.Count<PerWorldPawn>());
            Assert.Equal("replay pawn", Scripts.First<PerWorldPawn>()!.Name);
            Assert.Equal("replay pawn", pawn.GetScript<PerWorldPawn>()!.Name);
            Assert.Null(bot.GetScript<PerWorldPawn>());

            // A world with no scripts sees none; with no world known (0: an engine without the call), every one.
            s_world = 3;
            Assert.Equal(0, Scripts.Count<PerWorldPawn>());
            s_world = 0;
            Assert.Equal(3, Scripts.Count<PerWorldPawn>());
        }
        finally
        {
            Bind(false);
            s_world = 0;
            Instances.Clear();
            Order.Clear();
            Worlds.Clear();
        }
    }

    [Fact]
    public void Net_Events_keeps_a_ledger_per_session()
    {
        var events = typeof(Net).GetProperty("Events", BindingFlags.NonPublic | BindingFlags.Static)!;
        var ledgers = (NetEventLedgers)typeof(Net).GetField("EventLedgers", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
        Bind(true);
        try
        {
            ledgers.Clear();
            var pawn = new Entity(3, 1);
            // The live session (5) presents a shot at tick 100; the killcam's (6) replays the same tick with the same
            // entity id: its own, so it presents too; each still refuses its own repeat.
            s_session = 5;
            Assert.Equal(5, Net.SessionId);
            Assert.True(((NetEventLedger)events.GetValue(null)!).TryPresent(pawn, 100, 1));
            s_session = 6;
            Assert.True(((NetEventLedger)events.GetValue(null)!).TryPresent(pawn, 100, 1));
            Assert.False(((NetEventLedger)events.GetValue(null)!).TryPresent(pawn, 100, 1));
            s_session = 5;
            Assert.False(((NetEventLedger)events.GetValue(null)!).TryPresent(pawn, 100, 1));
            Assert.Equal(2, ledgers.Count);
            // The next killcam in a kept replay world is a session of its own: it starts empty.
            s_session = 7;
            Assert.True(((NetEventLedger)events.GetValue(null)!).TryPresent(pawn, 100, 1));
        }
        finally
        {
            Bind(false);
            s_session = 0;
            ledgers.Clear();
        }
    }

    [Fact]
    public void The_ledgers_keep_the_sessions_asked_for_most_recently()
    {
        var ledgers = new NetEventLedgers();
        var pawn = new Entity(3, 1);
        var live = ledgers.For(1);
        Assert.True(live.TryPresent(pawn, 10, 1));
        // Killcams come and go while the live session is asked for every tick.
        for (int session = 2; session < 2 + NetEventLedgers.MaxSessions * 3; ++session)
        {
            Assert.True(ledgers.For(session).TryPresent(pawn, 10, 1));
            Assert.Same(live, ledgers.For(1));
        }
        Assert.Equal(NetEventLedgers.MaxSessions, ledgers.Count);
        Assert.False(ledgers.For(1).TryPresent(pawn, 10, 1));
        // One not asked for in a while went: asked again, it starts over.
        Assert.True(ledgers.For(2).TryPresent(pawn, 10, 1));
        ledgers.Clear();
        Assert.Equal(0, ledgers.Count);
    }
}
