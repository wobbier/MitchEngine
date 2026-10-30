namespace ScriptCore;

/// <summary>The entity's Rigidbody: forces and velocity.</summary>
public class Rigidbody : Component
{
    /// <summary>Applies a force (see ForceMode for continuous or instant, with or without mass).</summary>
    public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force)
    {
        unsafe { Engine._api.Rigidbody_AddForce(Entity, &force, (int)mode); }
    }

    /// <summary>The body's velocity in metres per second.</summary>
    public Vector3 Velocity
    {
        get { unsafe { Vector3 v; Engine._api.Rigidbody_GetVelocity(Entity, &v); return v; } }
        set { unsafe { Engine._api.Rigidbody_SetVelocity(Entity, &value); } }
    }

    /// <summary>
    /// Moves the body (and its Transform) instantly, facing forward with up as close to up as
    /// possible. Keeps its velocity.
    /// </summary>
    public void Teleport(Vector3 position, Vector3 forward, Vector3 up)
    {
        unsafe { Engine._api.Rigidbody_Teleport(Entity, &position, &forward, &up); }
    }
}
