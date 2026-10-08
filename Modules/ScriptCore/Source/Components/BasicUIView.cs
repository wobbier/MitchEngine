namespace ScriptCore;

public class BasicUIView : Component
{
    public void ExecuteJS(string inJS)
    {
        unsafe { fixed (byte* p = Engine.Utf8(inJS)) Engine._api.BasicUIView_ExecuteJS(Entity, p); }
    }
}
