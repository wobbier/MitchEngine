namespace ScriptCore;

// The capsule character moved by the physics mover (Engine/Docs/Physics.md).
public class CharacterController : Component
{
    // Desired horizontal direction in world space; length <= 1 scales the speed. Kept until changed.
    public void SetMoveInput(Vector3 direction)
    {
        unsafe { Engine._api.Character_SetMoveInput(Entity, &direction); }
    }

    // A displacement added on the next physics step.
    public void Move(Vector3 displacement)
    {
        unsafe { Engine._api.Character_Move(Entity, &displacement); }
    }

    public void Jump()
    {
        unsafe { Engine._api.Character_Jump(Entity); }
    }

    public bool IsGrounded { get { unsafe { return Engine._api.Character_IsGrounded(Entity) != 0; } } }

    public Vector3 Velocity
    {
        get { unsafe { Vector3 v; Engine._api.Character_GetVelocity(Entity, &v); return v; } }
    }

    public float MaxSpeed
    {
        get { unsafe { return Engine._api.Character_GetMaxSpeed(Entity); } }
        set { unsafe { Engine._api.Character_SetMaxSpeed(Entity, value); } }
    }
}
