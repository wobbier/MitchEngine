using System;
using ScriptCore;
using Xunit;

// SimulatedInput recordings (ME-136 follow-up): the text form round-trips, sorts by time and
// rejects malformed entries.
public class SimulatedInputTests
{
    [Fact]
    public void Recording_parses_and_prints_its_text_form()
    {
        var recording = SimulatedInput.Recording.Parse("0 +W; 0.5 +Space\n0.6 -space; 2 -W");
        Assert.Equal(4, recording.Count);
        Assert.Equal(new SimulatedInput.Event(0.6f, KeyCode.Space, false), recording[2]);
        Assert.Equal("0 +W; 0.5 +Space; 0.6 -Space; 2 -W", recording.ToString());
        Assert.Equal(recording, SimulatedInput.Recording.Parse(recording.ToString()));
    }

    [Fact]
    public void Recording_sorts_events_by_time()
    {
        var recording = SimulatedInput.Recording.Parse("1 -E; 0.25 +E");
        Assert.Equal(new[] { 0.25f, 1f }, new[] { recording[0].Time, recording[1].Time });
    }

    [Fact]
    public void Recording_built_in_code_matches_the_parsed_form()
    {
        var built = new SimulatedInput.Recording { { 0f, KeyCode.W, true }, { 1.5f, KeyCode.W, false } };
        Assert.Equal(SimulatedInput.Recording.Parse("0 +W; 1.5 -W"), built);
    }

    [Theory]
    [InlineData("W")]
    [InlineData("0.5 W")]
    [InlineData("-1 +W")]
    [InlineData("0 +NotAKey")]
    [InlineData("0 +None")]
    [InlineData("zero +W")]
    public void Recording_rejects_malformed_entries(string text)
    {
        Assert.Throws<FormatException>(() => SimulatedInput.Recording.Parse(text));
    }
}
