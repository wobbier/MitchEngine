using System;
using System.Collections.Generic;
using ScriptCore.Kit;
using Xunit;

// NEXT-8: the beat clock's arithmetic, driven by hand (no engine): crossings, hitches, the mixer's
// block steps, loops, seeks and judging.
public class BeatClockTests
{
    private static (BeatClock Clock, List<int> Beats, List<int> Bars) Make(float bpm = 120f, int beatsPerBar = 4, double offset = 0.0)
    {
        var clock = new BeatClock(bpm, beatsPerBar, offset);
        var beats = new List<int>();
        var bars = new List<int>();
        clock.OnBeat += (beat, bar, inBar) => beats.Add(beat);
        clock.OnBar += bar => bars.Add(bar);
        return (clock, beats, bars);
    }

    [Fact]
    public void Each_beat_and_bar_fires_once_as_time_crosses_it()
    {
        var (clock, beats, bars) = Make();
        for (int i = 0; i < 20; ++i) clock.Advance(0.125, -1.0);   // 2.5 s at 120 bpm
        Assert.Equal(new[] { 0, 1, 2, 3, 4, 5 }, beats);
        Assert.Equal(new[] { 0, 1 }, bars);
        Assert.Equal(5, clock.Beat);
        Assert.Equal(1, clock.BeatInBar);
        Assert.Equal(0f, clock.BeatPhase, 3);
    }

    [Fact]
    public void A_hitch_fires_every_beat_it_skipped_in_order_up_to_the_cap()
    {
        var (clock, beats, _) = Make();
        clock.Advance(0.125, -1.0);
        clock.Advance(2.0, -1.0);
        Assert.Equal(new[] { 0, 1, 2, 3, 4 }, beats);
        clock.MaxCatchUp = 3;
        clock.Advance(5.0, -1.0);   // 14 beats crossed: only the newest 3
        Assert.Equal(new[] { 12, 13, 14 }, beats.GetRange(5, beats.Count - 5));
    }

    [Fact]
    public void The_mixers_block_steps_are_smoothed_monotonic_and_fire_each_beat_once()
    {
        var (clock, beats, _) = Make(bpm: 150f);
        double previous = 0.0;
        for (int frame = 1; frame <= 600; ++frame)
        {
            double wall = frame / 60.0;
            double mixer = Math.Floor(wall / 0.02) * 0.02;      // the position moves in 20 ms blocks
            clock.Advance(1.0 / 60.0, mixer);
            Assert.True(clock.SongTime >= previous, $"time went back at frame {frame}");
            Assert.True(Math.Abs(clock.SongTime - wall) < 0.025, $"drifted {clock.SongTime - wall:0.000} s at frame {frame}");
            previous = clock.SongTime;
        }
        // 10 s at 150 bpm: beats 0..24 (and 25 if the last block landed on it).
        Assert.Equal(beats.Count, new HashSet<int>(beats).Count);
        Assert.InRange(beats.Count, 25, 26);
        for (int i = 0; i < beats.Count; ++i) Assert.Equal(i, beats[i]);
    }

    [Fact]
    public void A_looped_track_starts_its_beats_again()
    {
        var (clock, beats, _) = Make();
        for (double t = 0.125; t <= 2.0; t += 0.125) clock.Advance(0.125, t);
        Assert.Equal(new[] { 0, 1, 2, 3, 4 }, beats);
        clock.Advance(0.125, 0.0);      // the loop point
        clock.Advance(0.125, 0.125);
        clock.Advance(0.375, 0.5);
        Assert.Equal(new[] { 0, 1, 2, 3, 4, 0, 1 }, beats);
    }

    [Fact]
    public void A_seek_back_into_a_beat_waits_for_the_next_one()
    {
        var (clock, beats, _) = Make();
        for (double t = 0.125; t <= 3.0; t += 0.125) clock.Advance(0.125, t);
        beats.Clear();
        clock.Advance(0.125, 1.25);     // into beat 2: not fired late
        Assert.Empty(beats);
        clock.Advance(0.25, 1.5);
        Assert.Equal(new[] { 3 }, beats);
    }

    [Fact]
    public void Beats_start_at_the_offset()
    {
        var (clock, beats, _) = Make(offset: 0.25);
        clock.Advance(0.125, -1.0);
        Assert.Empty(beats);
        Assert.True(clock.Beat < 0);
        clock.Advance(0.125, -1.0);
        Assert.Equal(new[] { 0 }, beats);
    }

    [Fact]
    public void NearestBeat_judges_early_and_late_presses_in_seconds()
    {
        var clock = new BeatClock(120f, offsetSeconds: 0.1);
        Assert.Equal(2, clock.NearestBeat(1.12, out double late));
        Assert.Equal(0.02, late, 6);
        Assert.Equal(3, clock.NearestBeat(1.57, out double early));
        Assert.Equal(-0.03, early, 6);
    }
}
