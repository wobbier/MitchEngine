namespace ScriptCore;

/// <summary>
/// The entity's Ragdoll (GAME-5): physics takes the bones while Simulating, and clearing it blends
/// back to the animation over BlendSeconds. Build its parts in the inspector (Build from skeleton).
///   GetComponent&lt;Ragdoll&gt;().Simulating = true;    // knocked out
/// </summary>
public class Ragdoll : Component
{
    /// <summary>Physics drives the bones while true; clearing it blends back to the animation over BlendSeconds.</summary>
    public bool Simulating
    {
        get => Entity.GetField("Ragdoll", "Simulating", false);
        set => Entity.SetField("Ragdoll", "Simulating", value);
    }

    /// <summary>Seconds to blend from the ragdoll pose back to the animation.</summary>
    public float BlendSeconds
    {
        get => Entity.GetField("Ragdoll", "BlendSeconds", 0.4f);
        set => Entity.SetField("Ragdoll", "BlendSeconds", value);
    }
}
