namespace ScriptCore;

// The entity's Ragdoll (GAME-5): physics takes the bones while Simulating, and clearing it blends
// back to the animation over BlendSeconds. Build its parts in the inspector (Build from skeleton).
//   GetComponent<Ragdoll>().Simulating = true;    // knocked out
public class Ragdoll : Component
{
    public bool Simulating
    {
        get => Entity.GetField("Ragdoll", "Simulating", false);
        set => Entity.SetField("Ragdoll", "Simulating", value);
    }

    public float BlendSeconds
    {
        get => Entity.GetField("Ragdoll", "BlendSeconds", 0.4f);
        set => Entity.SetField("Ragdoll", "BlendSeconds", value);
    }
}
