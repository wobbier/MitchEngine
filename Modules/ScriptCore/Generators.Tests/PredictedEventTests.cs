using ScriptCore;
using Xunit;

// ME-291: Net.PredictedEvent presents once per (entity, tick, event).
public class PredictedEventTests
{
    private sealed class EventProbe : Script
    {
        public int Shown;
        public int LastValue;
        public override void OnNetEvent(int eventId, int value)
        {
            Shown++;
            LastValue = value;
        }
    }


    [Fact]
    public void Offline_an_event_presents_immediately_every_time_it_is_raised()
    {
        Assert.Equal(0, Net.Role);
        var probe = new EventProbe();
        Net.PredictedEvent(probe, 1, 5);
        Net.PredictedEvent(probe, 1, 6);
        Assert.Equal(2, probe.Shown);
        Assert.Equal(6, probe.LastValue);
    }


    [Fact]
    public void The_ledger_presents_a_tick_s_event_once_across_resims_and_the_server_copy()
    {
        var ledger = new NetEventLedger();
        var pawn = new Entity(3, 1);
        var other = new Entity(4, 1);
        Assert.True(ledger.TryPresent(pawn, 100, 1));    // predicted
        Assert.False(ledger.TryPresent(pawn, 100, 1));   // resimulated after a correction
        Assert.False(ledger.TryPresent(pawn, 100, 1));   // the server's copy arrives
        Assert.True(ledger.TryPresent(pawn, 100, 2));    // another event, same tick
        Assert.True(ledger.TryPresent(pawn, 101, 1));    // the same event, a later tick
        Assert.True(ledger.TryPresent(other, 100, 1));   // another entity
        Assert.Equal(4, ledger.Count);
    }


    [Fact]
    public void The_ledger_forgets_ticks_older_than_its_window()
    {
        var ledger = new NetEventLedger();
        var pawn = new Entity(3, 1);
        for (int tick = 0; tick < 2000; ++tick)
        {
            Assert.True(ledger.TryPresent(pawn, tick, 1));
        }
        Assert.True(ledger.Count <= 1025);
        Assert.False(ledger.TryPresent(pawn, 1999, 1));
    }
}
