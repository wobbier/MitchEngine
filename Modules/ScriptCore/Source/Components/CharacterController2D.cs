namespace ScriptCore;

/// <summary>
/// The platformer capsule moved by Box2D's mover in the XY plane (Engine/Docs/Physics.md): runs
/// along slopes, jumps with a short coyote time, falls with gravity.
/// </summary>
public class CharacterController2D : Component
{
    /// <summary>
    /// Desired horizontal direction in [-1, 1]. Kept until changed.
    /// </summary>
    public void SetMoveInput(float horizontal)
    {
        unsafe { Engine._api.Character2D_SetMoveInput(Entity, horizontal); }
    }

    /// <summary>
    /// Jumps when on the ground, or within the controller's CoyoteTime of leaving it.
    /// </summary>
    public void Jump()
    {
        unsafe { Engine._api.Character2D_Jump(Entity); }
    }

    /// <summary>
    /// Moves the character instantly; clears its velocity.
    /// </summary>
    public void Teleport(Vector2 position)
    {
        unsafe { Engine._api.Character2D_Teleport(Entity, &position); }
    }

    public bool IsGrounded { get { unsafe { return Engine._api.Character2D_IsGrounded(Entity) != 0; } } }

    public Vector2 Velocity
    {
        get { unsafe { Vector2 v; Engine._api.Character2D_GetVelocity(Entity, &v); return v; } }
    }

    public float MaxSpeed
    {
        get { unsafe { return Engine._api.Character2D_GetMaxSpeed(Entity); } }
        set { unsafe { Engine._api.Character2D_SetMaxSpeed(Entity, value); } }
    }
}
