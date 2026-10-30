using System.Collections.Generic;
using ScriptCore.Dialogue;

namespace ScriptCore.Kit;

// Talk to it (GAME-8): runs a .dialogue file through the stock dialogue UI (put a UIDocument with
// Assets/UI/Dialogue/Dialogue.rml in the scene). Interact starts it and then continues past lines;
// the document's buttons pick choices. Variables are shared by every Conversation, so what was
// said in one is remembered in the next. <<event>>s are logged and published (DialogueEvent).
public class Conversation : Interactable
{
    public string File = "";
    public string Node = "";
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

    public Conversation() { Prompt = "Talk"; }

    public bool IsTalking => _runner?.IsRunning ?? false;

    public override void OnStart()
    {
        if (StartOnPlay) Begin();
    }

    public override string GetPrompt(Entity by) => IsTalking ? "Continue" : Prompt;

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

    public override void OnUpdate(float deltaTime)
    {
        if (_view == null || _runner == null) return;
        if (!_view.Update() && Active == this)
        {
            Active = null;
            _view.Refresh();
        }
    }

    public override void OnDestroy()
    {
        if (Active == this) Active = null;
    }
}
