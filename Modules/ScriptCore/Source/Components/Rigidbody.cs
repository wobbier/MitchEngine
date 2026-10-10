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
}
