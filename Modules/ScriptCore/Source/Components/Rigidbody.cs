namespace ScriptCore;

public class Rigidbody : Component
{
    public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force)
    {
        unsafe { Engine._api.Rigidbody_AddForce(Entity, &force, (int)mode); }
    }

    public Vector3 Velocity
    {
        get { unsafe { Vector3 v; Engine._api.Rigidbody_GetVelocity(Entity, &v); return v; } }
        set { unsafe { Engine._api.Rigidbody_SetVelocity(Entity, &value); } }
    }

    // Moves the body (and its Transform) instantly, facing forward with up as close to up as
    // possible. Keeps its velocity.
    public void Teleport(Vector3 position, Vector3 forward, Vector3 up)
    {
        unsafe { Engine._api.Rigidbody_Teleport(Entity, &position, &forward, &up); }
    }
}
