using System.Text;

namespace ScriptCore;

// The entity's Animator (CS-5 a): parameters drive its state machine's transitions, Play jumps (or
// fades) to a state. Markers placed in states arrive as EventBus AnimationMarker events.
public class Animator : Component
{
    public void Play(string state, float fadeSeconds = 0f)
    {
        unsafe { fixed (byte* s = Engine.Utf8(state)) Engine._api.Animator_Play(Entity, s, fadeSeconds); }
    }

    public void Stop() { unsafe { Engine._api.Animator_Stop(Entity); } }
    public bool IsPlaying { get { unsafe { return Engine._api.Animator_IsPlaying(Entity) != 0; } } }

    public void SetFloat(string name, float value) { unsafe { fixed (byte* n = Engine.Utf8(name)) Engine._api.Animator_SetFloat(Entity, n, value); } }
    public float GetFloat(string name) { unsafe { fixed (byte* n = Engine.Utf8(name)) return Engine._api.Animator_GetFloat(Entity, n); } }
    public void SetBool(string name, bool value) { unsafe { fixed (byte* n = Engine.Utf8(name)) Engine._api.Animator_SetBool(Entity, n, value ? (byte)1 : (byte)0); } }
    public bool GetBool(string name) { unsafe { fixed (byte* n = Engine.Utf8(name)) return Engine._api.Animator_GetBool(Entity, n) != 0; } }
    public void SetTrigger(string name) { unsafe { fixed (byte* n = Engine.Utf8(name)) Engine._api.Animator_SetTrigger(Entity, n); } }

    // The state playing on a layer ("" = the base layer).
    public string GetState(string layer = "")
    {
        var buffer = new byte[128];
        int length;
        unsafe { fixed (byte* l = Engine.Utf8(layer)) fixed (byte* b = buffer) length = Engine._api.Animator_GetState(Entity, l, b, buffer.Length); }
        return Encoding.UTF8.GetString(buffer, 0, System.Math.Clamp(length, 0, buffer.Length - 1));
    }

    // 0..1 through the current state's clip (it loops past 1).
    public float GetNormalizedTime(string layer = "") { unsafe { fixed (byte* l = Engine.Utf8(layer)) return Engine._api.Animator_GetNormalizedTime(Entity, l); } }
    public void SetLayerWeight(string layer, float weight) { unsafe { fixed (byte* l = Engine.Utf8(layer)) Engine._api.Animator_SetLayerWeight(Entity, l, weight); } }
}
