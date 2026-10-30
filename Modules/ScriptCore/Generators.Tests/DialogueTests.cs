using System;
using System.Collections.Generic;
using System.Linq;
using ScriptCore;
using ScriptCore.Dialogue;
using Xunit;

// GAME-8: the dialogue language and runner (the UI side is DialogueFlows in the game).
public class DialogueTests
{
    private const string Gate = @"
title: Gate
---
// A comment.
<<set $name = ""traveller"">>
Guard: Halt! Who goes there?
-> A friend.
    <<set $trusted = true>>
    Guard: Then pass, friend.
-> None of your business. <<if not $trusted>>
    Guard: Then you stay out.
    <<jump Refused>>
<<if $trusted and $visits >= 1>>
    Guard: Back again, {$name}?
<<elseif $trusted>>
    Guard: Mind the stairs, {$name}.
<<else>>
    Guard: Move along.
<<endif>>
<<set $visits = $visits + 1>>
<<event open-gate north>>
===
title: Refused
---
Old Man: The guard won't budge.
<<stop>>
Never shown.
===
";

    private static DialogueRunner Run(Dictionary<string, object>? variables = null)
    {
        return new DialogueRunner(DialogueScript.Parse(Gate), variables) { Translate = s => s };
    }

    [Fact]
    public void Lines_choices_variables_and_events_run_in_order()
    {
        var runner = Run();
        var events = new List<string>();
        runner.Event += (name, args) => events.Add(name + ":" + string.Join(",", args));
        Assert.Equal(DialogueStep.Line, runner.Start());
        Assert.Equal("Guard", runner.Speaker);
        Assert.Equal("Halt! Who goes there?", runner.Text);
        Assert.Equal(DialogueStep.Choice, runner.Continue());
        Assert.Equal(new[] { "A friend.", "None of your business." }, runner.Choices.Select(c => c.Text));
        Assert.Equal(DialogueStep.Line, runner.Choose(0));
        Assert.Equal("Then pass, friend.", runner.Text);
        Assert.Equal(DialogueStep.Line, runner.Continue());
        Assert.Equal("Mind the stairs, traveller.", runner.Text);
        Assert.Equal(DialogueStep.End, runner.Continue());
        Assert.Equal(new[] { "open-gate:north" }, events);
        Assert.Equal(true, runner.Variables["trusted"]);
        Assert.Equal(1.0, runner.Variables["visits"]);
    }

    [Fact]
    public void Shared_variables_change_what_the_next_conversation_offers()
    {
        var memory = new Dictionary<string, object>();
        var first = Run(memory);
        first.Start();
        first.Continue();
        first.Choose(0);
        while (first.IsRunning) first.Continue();

        var second = Run(memory);
        second.Start();
        Assert.Equal(DialogueStep.Choice, second.Continue());
        Assert.Single(second.Choices);   // "None of your business." hides once trusted
        second.Choose(0);
        second.Continue();
        Assert.Equal("Back again, traveller?", second.Text);
    }

    [Fact]
    public void Jumps_and_stop_end_where_they_should()
    {
        var runner = Run();
        runner.Start();
        runner.Continue();
        runner.Choose(1);
        Assert.Equal("Then you stay out.", runner.Text);
        Assert.Equal(DialogueStep.Line, runner.Continue());
        Assert.Equal("Refused", runner.Node);
        Assert.Equal("Old Man", runner.Speaker);
        Assert.Equal(DialogueStep.End, runner.Continue());
    }

    [Fact]
    public void Lines_are_translated_before_variables_fill_in()
    {
        var runner = Run();
        runner.Translate = s => s == "Halt! Who goes there?" ? "Halte ! Qui va là ?" : s.Replace("Mind the stairs", "Attention aux marches");
        runner.Start();
        Assert.Equal("Halte ! Qui va là ?", runner.Text);
        runner.Continue();
        runner.Choose(0);
        runner.Continue();
        Assert.Equal("Attention aux marches, traveller.", runner.Text);
    }

    [Fact]
    public void Expressions_and_errors()
    {
        var vars = new Dictionary<string, object> { ["a"] = 3.0, ["b"] = "x" };
        Assert.Equal(true, Expression.Parse("$a * 2 > 5 and not ($b == \"y\")", "t", 1).Evaluate(vars));
        Assert.Equal("x1", Expression.Parse("$b + 1", "t", 1).Evaluate(vars));
        Assert.Equal(false, Expression.Parse("$missing", "t", 1).Evaluate(vars));
        Assert.Throws<FormatException>(() => DialogueScript.Parse("title: A\n---\n<<if $x>>\nHi\n===\n"));
        Assert.Throws<FormatException>(() => DialogueScript.Parse("title: A\n---\n<<dance>>\n===\n"));
        Assert.Throws<FormatException>(() => Expression.Parse("1 +", "t", 1));
    }
}
