using System;
using System.Runtime.CompilerServices;

public class BasicUIView
    : Component
{
    public void ExecuteJS(string inJS)
    {
        BasicUIView_ExecuteJS(EntID, inJS);
    }

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public extern static void BasicUIView_ExecuteJS(EntityID id, string inJS);
}
