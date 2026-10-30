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

    // Local Euler angles in degrees. Readback can return an equivalent set of angles;
    // use Rotate for continuous motion instead of reading and incrementing one Euler axis.
    public Vector3 Rotation
    {
        get { unsafe { Vector3 v; Engine._api.Transform_GetRotation(Entity, &v); return v; } }
        set { unsafe { Engine._api.Transform_SetRotation(Entity, &value); } }
    }

    /// <summary>
    /// Rotates by degrees about the local axes using quaternion composition.
    /// </summary>
    public void Rotate(Vector3 degrees)
    {
        unsafe { Engine._api.Transform_Rotate(Entity, &degrees); }
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

    /// <summary>
    /// Turns to face a world direction, with up as close to the given up as possible.
    /// </summary>
    public void LookDirection(Vector3 forward, Vector3 up)
    {
        unsafe { Engine._api.Transform_LookDirection(Entity, &forward, &up); }
    }

    /// <summary>
    /// The parent entity (Entity.Null at the scene root); setting it keeps the world pose.
    /// </summary>
    public Entity Parent
    {
        get { unsafe { Entity e; Engine._api.Transform_GetParent(Entity, &e); return e; } }
        set { unsafe { Engine._api.Transform_SetParent(Entity, value); } }
    }
}
