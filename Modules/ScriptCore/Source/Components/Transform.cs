namespace ScriptCore;

public class Transform : Component
{
    public Vector3 Position
    {
        get { unsafe { Vector3 v; Engine._api.Transform_GetTranslation(Entity, &v); return v; } }
        set { unsafe { Engine._api.Transform_SetTranslation(Entity, &value); } }
    }

    public Vector3 Scale
    {
        get { unsafe { Vector3 v; Engine._api.Transform_GetScale(Entity, &v); return v; } }
        set { unsafe { Engine._api.Transform_SetScale(Entity, &value); } }
    }

    // Euler
    public Vector3 Rotation
    {
        get { unsafe { Vector3 v; Engine._api.Transform_GetRotation(Entity, &v); return v; } }
        set { unsafe { Engine._api.Transform_SetRotation(Entity, &value); } }
    }
}
