using System.Collections.Generic;
using ScriptCore.Dialogue;

namespace ScriptCore.Kit;

/// <summary>
/// Talk to it (GAME-8): runs a .dialogue file through the stock dialogue UI (put a UIDocument with
/// Assets/UI/Dialogue/Dialogue.rml in the scene). Interact starts it and then continues past lines;
/// the document's buttons pick choices. Variables are shared by every Conversation, so what was
/// said in one is remembered in the next. &lt;&lt;event&gt;&gt;s are logged and published (DialogueEvent).
/// </summary>
public class Conversation : Interactable
{
    /// <summary>The .dialogue file.</summary>
    public string File = "";
    /// <summary>The node to start at (empty: the first).</summary>
    public string Node = "";
    /// <summary>Start talking when the game starts.</summary>
    public bool StartOnPlay = false;

    /// <summary>
    /// Shared by every conversation (what the player said earlier).
    /// </summary>
    public static readonly Dictionary<string, object> Variables = new();
    /// <summary>
    /// The conversation on screen, if any (a controller can stop walking while it's set).
    /// </summary>
    public static Conversation? Active { get; private set; }

    private DialogueView? _view;
    private DialogueRunner? _runner;

    /// <summary>A conversation that says "Talk".</summary>
    public Conversation() { Prompt = "Talk"; }

    /// <summary>Whether the conversation is running.</summary>
    public bool IsTalking => _runner?.IsRunning ?? false;

    /// <inheritdoc/>
    public override void OnStart()
    {
        if (StartOnPlay) Begin();
    }

    /// <inheritdoc/>
    public override string GetPrompt(Entity by) => IsTalking ? "Continue" : Prompt;

    /// <inheritdoc/>
    public override void Interact(Entity by)
    {
        if (!IsTalking)
        {
            Begin();
        }
        else if (_runner!.Current == DialogueStep.Line)
        {
            _runner.Continue();
            _view?.Refresh();
        }
    }

    /// <summary>Starts the conversation; false when the file doesn't load.</summary>
    public bool Begin()
    {
        var script = DialogueFiles.Load(File);
        if (script == null) return false;
        _view ??= new DialogueView();
        _runner = new DialogueRunner(script, Variables);
        _runner.Event += (name, args) => Debug.Log($"Dialogue: event {name}{(args.Length > 0 ? " " + string.Join(" ", args) : "")}");
        _runner.Start(Node);
        Active = this;
        _view.Show(_runner);
        return IsTalking;
    }

    /// <inheritdoc/>
    public override void OnUpdate(float deltaTime)
    {
        if (_view == null || _runner == null) return;
        if (!_view.Update() && Active == this)
        {
            Active = null;
            _view.Refresh();
        }
    }

    /// <inheritdoc/>
    public override void OnDestroy()
    {
        if (Active == this) Active = null;
    }
}
