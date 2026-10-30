using System.Collections.Generic;
using ScriptCore;
using ScriptCore.AI;
using Xunit;

// GAME-3: behavior trees, the view cone and noise.
public class AITests
{
    private static BTContext Tick(float dt = 0.1f) => new(new Blackboard(), dt);

    [Fact]
    public void Selectors_fall_through_and_higher_branches_interrupt_lower_ones()
    {
        bool alarm = false;
        var log = new List<string>();
        int resets = 0;
        var tree = BT.Selector(
            BT.Sequence(BT.Condition(() => alarm), BT.Action(() => { log.Add("chase"); return BTStatus.Running; })),
            BT.Action(() => { log.Add("patrol"); return BTStatus.Running; }, () => ++resets));
        Assert.Equal(BTStatus.Running, tree.Tick(Tick()));
        alarm = true;
        tree.Tick(Tick());
        alarm = false;
        tree.Tick(Tick());
        Assert.Equal(new[] { "patrol", "chase", "patrol" }, log);
        Assert.Equal(1, resets);   // patrol was interrupted once
    }

    [Fact]
    public void Memory_sequences_resume_and_waits_take_their_time()
    {
        int done = 0;
        var tree = BT.MemorySequence(BT.Wait(0.25f), BT.Do(() => ++done));
        Assert.Equal(BTStatus.Running, tree.Tick(Tick()));
        Assert.Equal(BTStatus.Running, tree.Tick(Tick()));
        Assert.Equal(BTStatus.Success, tree.Tick(Tick()));
        Assert.Equal(1, done);
        Assert.Equal(BTStatus.Failure, BT.Not(BT.Condition(() => true)).Tick(Tick()));
        var cooled = BT.Cooldown(1f, BT.Do(() => ++done));
        cooled.Tick(Tick());
        Assert.Equal(BTStatus.Failure, cooled.Tick(Tick()));
        Assert.Equal(2, done);
    }

    [Fact]
    public void Blackboards_hold_typed_values()
    {
        var board = new Blackboard();
        board.Set("alert", 2);
        Assert.Equal(2, board.Get<int>("alert"));
        Assert.Equal("none", board.Get("missing", "none"));
        Assert.Equal(0f, board.Get<float>("alert"));   // wrong type: the fallback
    }

    [Fact]
    public void The_view_cone_checks_range_and_angle()
    {
        var eye = new Vector3(0f, 1f, 0f);
        var forward = new Vector3(0f, 0f, 1f);
        Assert.True(Perception.InCone(eye, forward, new Vector3(1f, 1f, 5f), 12f, 120f));
        Assert.False(Perception.InCone(eye, forward, new Vector3(0f, 1f, -5f), 12f, 120f));   // behind
        Assert.False(Perception.InCone(eye, forward, new Vector3(5f, 1f, 1f), 12f, 120f));    // 79 degrees off
        Assert.False(Perception.InCone(eye, forward, new Vector3(0f, 1f, 20f), 12f, 120f));   // too far
    }

    [Fact]
    public void Noises_are_read_once()
    {
        int start = Noise.Count;
        Noise.Make(new Vector3(1f, 0f, 0f), 5f);
        Noise.Make(new Vector3(2f, 0f, 0f), 5f);
        Assert.Equal(2, System.Linq.Enumerable.Count(Noise.Since(start)));
        Assert.Empty(Noise.Since(Noise.Count));
    }
}
