using System;
using System.Runtime.CompilerServices;

public class Camera
    : Component
{
    public Vector3 ClearColor
    {
        get
        {
            Camera_GetClearColor(Parent.EntID, out Vector3 clearColor);
            return clearColor;
        }
        set
        {
            Camera_SetClearColor(Parent.EntID, ref value);
        }
    }

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void Camera_GetClearColor(EntityID id, out Vector3 clearColor);

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void Camera_SetClearColor(EntityID id, ref Vector3 clearColor);
}
