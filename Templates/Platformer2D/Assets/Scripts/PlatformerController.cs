using ScriptCore;

// Side-on platformer movement. Goes on the body (with a CharacterController2D): Move's X runs,
// Sprint runs faster, Jump jumps. The controller's coyote time still allows a jump just after
// running off a ledge, and a press just before landing is remembered for JumpBuffer seconds. The
// sprite faces where it runs and plays Walk while moving on the ground, Idle otherwise. The
// actions live in Assets/Config/Input.inputactions.
public class PlatformerController : Script
{
    public float RunSpeed = 6.0f;
    public float SprintSpeed = 10.0f;
    [Tooltip("Seconds a jump pressed just before landing is remembered")]
    public float JumpBuffer = 0.12f;

    private CharacterController2D character;
    private SpriteRenderer sprite;
    private Animator animator;
    private float jumpBuffered;
    private bool walking;

    public override void OnStart()
    {
        character = GetComponent<CharacterController2D>();
        sprite = GetComponent<SpriteRenderer>();
        animator = GetComponent<Animator>();
        walking = false;
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

        float run = Input.GetActionVector2("Move").x;
        character.MaxSpeed = Input.IsActionPressed("Sprint") ? SprintSpeed : RunSpeed;
        character.SetMoveInput(run);

        // A press jumps now (on the ground, or within the coyote time); in the air, it waits a
        // moment for the landing.
        if (Input.WasActionPressed("Jump"))
        {
            jumpBuffered = character.IsGrounded ? 0f : JumpBuffer;
            character.Jump();
        }
        else if (jumpBuffered > 0f)
        {
            jumpBuffered -= deltaTime;
            if (character.IsGrounded && character.Velocity.y <= 0f)
            {
                character.Jump();
                jumpBuffered = 0f;
            }
        }

        if (sprite != null)
        {
            if (run > 0.1f)
            {
                sprite.FlipX = false;
            }
            else if (run < -0.1f)
            {
                sprite.FlipX = true;
            }
        }
        bool moving = character.IsGrounded && Mathf.Abs(character.Velocity.x) > 0.5f;
        if (animator != null && moving != walking)
        {
            walking = moving;
            animator.Play(moving ? "Walk" : "Idle");
        }
    }
}
