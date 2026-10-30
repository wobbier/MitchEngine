using ScriptCore;
using Xunit;

// PLAY-7: the caption queue (the strip itself is checked in AccessibilityFlows).
public class CaptionTests
{
    [Fact]
    public void Lines_last_as_long_as_reading_takes_and_three_show_at_once()
    {
        Assert.Equal(2f, CaptionLines.ReadingSeconds("Hi"));
        Assert.Equal(8f, CaptionLines.ReadingSeconds(new string('a', 500)));
        var lines = new CaptionLines();
        Assert.Equal(3f, lines.Add("Guard", "Who goes there?", 3f, 10f));
        lines.Add("", "[door creaks]", 1f, 10f);
        lines.Add("A", "one", 5f, 10f);
        lines.Add("B", "two", 5f, 10f);
        Assert.Equal(3, lines.Current.Count);
        Assert.Equal("[door creaks]", lines.Current[0].Text);   // the oldest went
        lines.Expire(11.5f);
        Assert.Equal(new[] { "one", "two" }, new[] { lines.Current[0].Text, lines.Current[1].Text });
        lines.Expire(20f);
        Assert.Empty(lines.Current);
    }
}
