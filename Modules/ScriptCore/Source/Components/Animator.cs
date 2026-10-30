using System.Text;

namespace ScriptCore;

/// <summary>
/// The entity's Animator (CS-5 a): parameters drive its state machine's transitions, Play jumps (or
/// fades) to a state. Markers placed in states arrive as EventBus AnimationMarker events.
/// </summary>
public class Animator : Component
{
    /// <summary>Jumps to a state, or cross-fades into it over fadeSeconds.</summary>
    public void Play(string state, float fadeSeconds = 0f)
    {
        unsafe { fixed (byte* s = Engine.Utf8(state, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Animator_Play(Entity, s, fadeSeconds); }
    }

    /// <summary>Stops the animation (the pose stays).</summary>
    public void Stop() { unsafe { Engine._api.Animator_Stop(Entity); } }
    /// <summary>Whether a state is playing.</summary>
    public bool IsPlaying { get { unsafe { return Engine._api.Animator_IsPlaying(Entity) != 0; } } }

    /// <summary>Sets a float parameter (transition conditions, blend trees).</summary>
    public void SetFloat(string name, float value) { unsafe { fixed (byte* n = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Animator_SetFloat(Entity, n, value); } }
    /// <summary>A float parameter's value.</summary>
    public float GetFloat(string name) { unsafe { fixed (byte* n = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Animator_GetFloat(Entity, n); } }
    /// <summary>Sets a bool parameter.</summary>
    public void SetBool(string name, bool value) { unsafe { fixed (byte* n = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Animator_SetBool(Entity, n, value ? (byte)1 : (byte)0); } }
    /// <summary>A bool parameter's value.</summary>
    public bool GetBool(string name) { unsafe { fixed (byte* n = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Animator_GetBool(Entity, n) != 0; } }
    /// <summary>Sets a trigger: true until a transition consumes it.</summary>
    public void SetTrigger(string name) { unsafe { fixed (byte* n = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Animator_SetTrigger(Entity, n); } }

    /// <summary>
    /// The state playing on a layer ("" = the base layer).
    /// </summary>
    public string GetState(string layer = "")
    {
        var buffer = new byte[128];
        int length;
        unsafe { fixed (byte* l = Engine.Utf8(layer, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* b = buffer) length = Engine._api.Animator_GetState(Entity, l, b, buffer.Length); }
        return Encoding.UTF8.GetString(buffer, 0, System.Math.Clamp(length, 0, buffer.Length - 1));
    }

    /// <summary>
    /// 0..1 through the current state's clip (it loops past 1).
    /// </summary>
    public float GetNormalizedTime(string layer = "") { unsafe { fixed (byte* l = Engine.Utf8(layer, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.Animator_GetNormalizedTime(Entity, l); } }
    /// <summary>A layer's blend weight, 0..1.</summary>
    public void SetLayerWeight(string layer, float weight) { unsafe { fixed (byte* l = Engine.Utf8(layer, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Animator_SetLayerWeight(Entity, l, weight); } }
}
