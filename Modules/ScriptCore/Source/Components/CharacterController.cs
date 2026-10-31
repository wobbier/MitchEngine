namespace ScriptCore;

/// <summary>
/// The capsule character moved by the physics mover (Engine/Docs/Physics.md).
/// </summary>
public class CharacterController : Component
{
    /// <summary>
    /// Desired horizontal direction in world space; length &lt;= 1 scales the speed. Kept until changed.
    /// </summary>
    public void SetMoveInput(Vector3 direction)
    {
        unsafe { Engine._api.Character_SetMoveInput(Entity, &direction); }
    }

    /// <summary>
    /// A displacement added on the next physics step.
    /// </summary>
    public void Move(Vector3 displacement)
    {
        unsafe { Engine._api.Character_Move(Entity, &displacement); }
    }

    /// <summary>
    /// Moves the character instantly, turned to face forward (flattened: characters stay upright).
    /// Clears its velocity; set Velocity afterwards to keep moving.
    /// </summary>
    public void Teleport(Vector3 position, Vector3 forward)
    {
        unsafe { Engine._api.Character_Teleport(Entity, &position, &forward); }
    }

    /// <summary>Jumps when grounded (the controller's jump speed).</summary>
    public void Jump()
    {
        unsafe { Engine._api.Character_Jump(Entity); }
    }

    /// <summary>
    /// Where the simulation has it: the capsule centre (plus Center) after the last physics step,
    /// a Teleport or a network correction, the same in OnFixedUpdate on every peer and in
    /// resimulations. The Transform is the interpolated pose drawn between steps: read this in
    /// simulation code (aiming, spawning shots), the Transform for presentation.
    /// </summary>
    public Vector3 Position
    {
        get { unsafe { Vector3 p; Engine._api.Character_GetPosition(Entity, &p); return p; } }
    }

    /// <summary>Whether the character stands on something.</summary>
    public bool IsGrounded { get { unsafe { return Engine._api.Character_IsGrounded(Entity) != 0; } } }

    /// <summary>The character's velocity in metres per second.</summary>
    public Vector3 Velocity
    {
        get { unsafe { Vector3 v; Engine._api.Character_GetVelocity(Entity, &v); return v; } }
        set { unsafe { Engine._api.Character_SetVelocity(Entity, &value); } }
    }

    /// <summary>The top walking speed in metres per second.</summary>
    public float MaxSpeed
    {
        get { unsafe { return Engine._api.Character_GetMaxSpeed(Entity); } }
        set { unsafe { Engine._api.Character_SetMaxSpeed(Entity, value); } }
    }
}
