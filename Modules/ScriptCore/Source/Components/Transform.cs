using System;
using System.Runtime.CompilerServices;

public class Transform
    : Component
{
    public Vector3 Translation
    {
        get
        {
            Entity_GetTranslation(Parent.EntID, out Vector3 translation);
            return translation;
        }
        set
        {
            Entity_SetTranslation(Parent.EntID, ref value);
        }
    }

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void Entity_GetTranslation(EntityID id, out Vector3 translation);

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void Entity_SetTranslation(EntityID id, ref Vector3 translation);


    public Vector3 Scale
    {
        get
        {
            Transform_GetScale(Parent.EntID, out Vector3 scale);
            return scale;
        }
        set
        {
            Transform_SetScale(Parent.EntID, ref value);
        }
    }

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void Transform_GetScale(EntityID id, out Vector3 scale);

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static void Transform_SetScale(EntityID id, ref Vector3 scale);
}
