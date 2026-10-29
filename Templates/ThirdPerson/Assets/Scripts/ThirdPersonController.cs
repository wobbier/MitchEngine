using ScriptCore;

// Third-person movement. Goes on the body (with a CharacterController): Move steers it relative to
// the camera named in CameraName, Sprint runs, Jump jumps, and the body turns to face where it goes.
// The actions live in Assets/Config/Input.inputactions.
public class ThirdPersonController : Script
{
    public string CameraName = "Main Camera";
    public float WalkSpeed = 5.0f;
    public float SprintSpeed = 9.0f;
    [Tooltip("Seconds to turn halfway to the move direction")]
    public float TurnHalfLife = 0.06f;

    private CharacterController character;
    private Entity cameraEntity;
    private Vector3 facing = Vector3.Forward;

    public override void OnStart()
    {
        character = GetComponent<CharacterController>();
        cameraEntity = World.Find(CameraName);
        facing = new Vector3(transform.Forward.x, 0f, transform.Forward.z).Normalized();
    }

    public override void OnReload()
    {
        OnStart();
    }

    public override void OnUpdate(float deltaTime)
    {
        if (character == null)
        {
            return;
        }

        // Camera-relative input on the ground plane.
        Vector2 move = Input.GetActionVector2("Move");
        Vector3 forward = Vector3.Forward;
        Vector3 right = Vector3.Right;
        if (cameraEntity)
        {
            Transform view = cameraEntity.Transform;
            forward = new Vector3(view.Forward.x, 0f, view.Forward.z).Normalized();
            right = new Vector3(view.Right.x, 0f, view.Right.z).Normalized();
        }
        Vector3 direction = right * move.x + forward * move.y;
        if (direction.LengthSquared() > 1f)
        {
            direction = direction.Normalized();
        }

        character.MaxSpeed = Input.IsActionPressed("Sprint") ? SprintSpeed : WalkSpeed;
        character.SetMoveInput(direction);
        if (Input.WasActionPressed("Jump") && character.IsGrounded)
        {
            character.Jump();
        }

        if (direction.LengthSquared() > 0.01f)
        {
            facing = Mathf.Damp(facing, direction.Normalized(), TurnHalfLife, deltaTime).Normalized();
            transform.LookAt(transform.WorldPosition + facing);
        }
    }
}
