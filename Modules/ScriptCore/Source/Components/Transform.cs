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

    public Vector3 WorldPosition
    {
        get { unsafe { Vector3 v; Engine._api.Transform_GetWorldPosition(Entity, &v); return v; } }
        set { unsafe { Engine._api.Transform_SetWorldPosition(Entity, &value); } }
    }

    public Vector3 Forward { get { unsafe { Vector3 v; Engine._api.Transform_GetForward(Entity, &v); return v; } } }
    public Vector3 Right { get { unsafe { Vector3 v; Engine._api.Transform_GetRight(Entity, &v); return v; } } }
    public Vector3 Up { get { unsafe { Vector3 v; Engine._api.Transform_GetUp(Entity, &v); return v; } } }

    public void LookAt(Vector3 worldTarget)
    {
        unsafe { Engine._api.Transform_LookAt(Entity, &worldTarget); }
    }

    // The parent entity (Entity.Null at the scene root); setting it keeps the world pose.
    public Entity Parent
    {
        get { unsafe { Entity e; Engine._api.Transform_GetParent(Entity, &e); return e; } }
        set { unsafe { Engine._api.Transform_SetParent(Entity, value); } }
    }
}
