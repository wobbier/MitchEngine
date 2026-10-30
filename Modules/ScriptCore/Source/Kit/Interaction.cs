using System;

namespace ScriptCore.Kit;

// Something an Interactor can use: a door, a lever, a terminal. Subclass and implement Interact.
public abstract class Interactable : Script
{
    public string Prompt = "Use";
    public bool Enabled = true;

    // The prompt shown for this actor ("Open", "Locked (needs Key)").
    public virtual string GetPrompt(Entity by) => Prompt;
    public abstract void Interact(Entity by);
}

// An actor's "press E to use": each Update finds the nearest Interactable within Range (in front
// of it), shows its prompt through a UI model (Model, keys "prompt" and "visible") and uses it when
// Action is pressed. Owned by the actor's script, which calls Update(Entity) every frame.
public sealed class Interactor
{
    public float Range = 2.5f;
    public string Action = "Interact";
    public string Model = "interact";

    public Interactable? Current { get; private set; }
    private string _shown = "\\0";

    // Picks, shows and (on the action) uses. Returns what was used, if anything.
    public Interactable? Update(Entity self, Vector3? forward = null)
    {
        var transform = self.Transform;
        var origin = transform.WorldPosition;
        var facing = forward ?? transform.Forward;
        Interactable? best = null;
        float bestScore = float.MaxValue;
        foreach (var candidate in ScriptBridge.AllScripts<Interactable>())
        {
            if (!candidate.Enabled || candidate.Entity == self || !candidate.Entity.IsAlive) continue;
            var offset = candidate.Entity.Transform.WorldPosition - origin;
            float distance = offset.Length();
            if (distance > Range) continue;
            float ahead = distance > 1e-4f ? Vector3.Dot(offset / distance, facing) : 1f;
            if (ahead < -0.2f) continue;   // behind
            float score = distance - ahead;
            if (score < bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        }
        Current = best;
        string prompt = best?.GetPrompt(self) ?? "";
        if (prompt != _shown)
        {
            var model = UI.Model(Model);
            model.Set("prompt", prompt);
            model.Set("visible", best != null);
            _shown = prompt;
        }
        if (best != null && Input.WasActionPressed(Action))
        {
            best.Interact(self);
            return best;
        }
        return null;
    }

    // Uses the current Interactable now (scripts and tests).
    public bool Use(Entity self)
    {
        if (Current == null) return false;
        Current.Interact(self);
        return true;
    }
}
