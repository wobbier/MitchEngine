using System;
using System.Collections.Generic;
using System.Text;

namespace ScriptCore.Dialogue;

/// <summary>Where a DialogueRunner is.</summary>
public enum DialogueStep
{
    /// <summary>Speaker / Text show a line; Continue() moves on</summary>
    Line,
    /// <summary>Choices lists the options; Choose(i) picks one</summary>
    Choice,
    /// <summary>The conversation is over (or never started).</summary>
    End,
}

/// <summary>An option offered at a choice.</summary>
/// <param name="Index">What Choose takes.</param>
/// <param name="Text">The option's text, localized.</param>
public readonly record struct DialogueChoice(int Index, string Text);

/// <summary>
/// Raised on the EventBus by &lt;&lt;event name args&gt;&gt;, and when a conversation ends.
/// </summary>
public readonly record struct DialogueEvent(string Name, string[] Args);
/// <summary>A conversation ended (EventBus).</summary>
/// <param name="Node">The node it ended in.</param>
public readonly record struct DialogueEndedEvent(string Node);

/// <summary>
/// Steps through a DialogueScript: Start(node), then Continue() past lines and Choose(i) at choices
/// until End. Variables live in a dictionary that can be shared between runners (what the player
/// said earlier). Text goes through Translate (localization keys = the source text) and then {$var}.
/// </summary>
public sealed class DialogueRunner
{
    private readonly DialogueScript _script;
    private readonly Stack<(List<Statement> Block, int Index)> _stack = new();
    private readonly List<Option> _options = new();
    private readonly List<DialogueChoice> _choices = new();

    /// <summary>A runner over a script, sharing variables when given (what the player said in earlier conversations).</summary>
    public DialogueRunner(DialogueScript script, Dictionary<string, object>? variables = null)
    {
        _script = script;
        Variables = variables ?? new Dictionary<string, object>();
    }

    /// <summary>The conversation's variables ($name without the $).</summary>
    public Dictionary<string, object> Variables { get; }
    /// <summary>
    /// Localizes a line or option (default: the engine's Loc, untranslated outside the engine).
    /// </summary>
    public Func<string, string>? Translate;
    /// <summary>&lt;&lt;event name args&gt;&gt; commands, as they run (also published as DialogueEvent).</summary>
    public event Action<string, string[]>? Event;

    /// <summary>Where the conversation is.</summary>
    public DialogueStep Current { get; private set; } = DialogueStep.End;
    /// <summary>Whether it hasn't ended.</summary>
    public bool IsRunning => Current != DialogueStep.End;
    /// <summary>The node it's in.</summary>
    public string Node { get; private set; } = "";
    /// <summary>The current line's speaker (empty for narration).</summary>
    public string Speaker { get; private set; } = "";
    /// <summary>The current line's text, localized and interpolated.</summary>
    public string Text { get; private set; } = "";
    /// <summary>The options at a choice.</summary>
    public IReadOnlyList<DialogueChoice> Choices => _choices;

    /// <summary>Starts at a node (empty: the first) and runs to the first line or choice.</summary>
    public DialogueStep Start(string node = "")
    {
        _stack.Clear();
        if (!Enter(node.Length > 0 ? node : _script.FirstNode)) return Finish();
        return Advance();
    }

    /// <summary>Moves past the current line to the next line or choice.</summary>
    public DialogueStep Continue() => Current == DialogueStep.Line ? Advance() : Current;

    /// <summary>Picks an option at a choice and runs on to the next line or choice.</summary>
    public DialogueStep Choose(int index)
    {
        if (Current != DialogueStep.Choice || index < 0 || index >= _options.Count) return Current;
        var option = _options[index];
        _stack.Push((option.Body, 0));
        return Advance();
    }

    /// <summary>Ends the conversation now.</summary>
    public void Stop()
    {
        if (IsRunning) Finish();
    }

    private bool Enter(string node)
    {
        if (!_script.Nodes.TryGetValue(node, out var body))
        {
            Warn($"Dialogue: no node '{node}'");
            return false;
        }
        _stack.Clear();
        _stack.Push((body, 0));
        Node = node;
        return true;
    }

    private DialogueStep Finish()
    {
        _stack.Clear();
        _choices.Clear();
        _options.Clear();
        Speaker = Text = "";
        Current = DialogueStep.End;
        EventBus.Publish(new DialogueEndedEvent(Node));
        return Current;
    }

    private DialogueStep Advance()
    {
        _choices.Clear();
        _options.Clear();
        for (int guard = 0; guard < 100000; ++guard)
        {
            if (_stack.Count == 0) return Finish();
            var (block, index) = _stack.Pop();
            if (index >= block.Count) continue;
            _stack.Push((block, index + 1));
            switch (block[index])
            {
                case LineStatement line:
                    if (line.Condition != null && !Expression.Truthy(line.Condition.Evaluate(Variables))) break;
                    Speaker = line.Speaker;
                    Text = Render(line.Text);
                    return Current = DialogueStep.Line;
                case ChoiceStatement choice:
                    foreach (var option in choice.Options)
                    {
                        if (option.Condition != null && !Expression.Truthy(option.Condition.Evaluate(Variables))) continue;
                        _choices.Add(new DialogueChoice(_options.Count, Render(option.Text)));
                        _options.Add(option);
                    }
                    if (_options.Count == 0) break;
                    return Current = DialogueStep.Choice;
                case JumpStatement jump:
                    if (!Enter(jump.Node)) return Finish();
                    break;
                case StopStatement:
                    return Finish();
                case SetStatement set:
                    Variables[set.Variable] = set.Value.Evaluate(Variables);
                    break;
                case IfStatement branch:
                    List<Statement>? taken = branch.Else;
                    foreach (var (condition, body) in branch.Branches)
                    {
                        if (Expression.Truthy(condition.Evaluate(Variables))) { taken = body; break; }
                    }
                    if (taken != null) _stack.Push((taken, 0));
                    break;
                case EventStatement raised:
                    Event?.Invoke(raised.Name, raised.Args);
                    EventBus.Publish(new DialogueEvent(raised.Name, raised.Args));
                    break;
            }
        }
        Warn($"Dialogue: '{Node}' loops without showing anything");
        return Finish();
    }

    private string Render(string text)
    {
        string translated = (Translate ?? DefaultTranslate)(text);
        if (translated.IndexOf("{$", StringComparison.Ordinal) < 0) return translated;
        var result = new StringBuilder();
        int i = 0;
        while (i < translated.Length)
        {
            int open = translated.IndexOf("{$", i, StringComparison.Ordinal);
            int close = open >= 0 ? translated.IndexOf('}', open) : -1;
            if (open < 0 || close < 0)
            {
                result.Append(translated, i, translated.Length - i);
                break;
            }
            result.Append(translated, i, open - i);
            string name = translated.Substring(open + 2, close - open - 2);
            result.Append(Variables.TryGetValue(name, out var value) ? Expression.Format(value) : "");
            i = close + 1;
        }
        return result.ToString();
    }

    private static unsafe void Warn(string message)
    {
        if (Engine._api.LogWarning != null) Debug.Warning(message);
        else Console.WriteLine(message);
    }

    private static unsafe string DefaultTranslate(string key) => Engine._api.Loc_Get != null ? Loc.Get(key) : key;
}
