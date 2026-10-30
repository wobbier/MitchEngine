using ScriptCore;
using ScriptCore.Kit;

// First-person walking. Goes on the body (with a CharacterController); the camera is a child entity
// at eye height, named in CameraName. Look turns the body left and right and tilts the camera up and
// down; Move walks, Sprint runs, Jump jumps. Pause (Escape / Start) frees the cursor, Fire captures
// it again. The actions live in Assets/Config/Input.inputactions.
//
// Turning is applied to the body's current facing rather than a stored angle, so anything else that
// turns the body (a teleporter, a cutscene) just works.
//
// The body is also the starter kit's actor (ScriptCore.Kit): it has Health (damage zones hurt it;
// at zero it comes back at the last Checkpoint it walked through), an Inventory (Pickups fill it,
// locked Doors check it) and an Interactor (Interact / E uses the Door or other Interactable in
// front of it). The HUD (Assets/UI/Hud.rml) shows health and the interaction prompt.
public class FirstPersonController : Script, IHealth, IInventory
{
    public string CameraName = "Main Camera";
    [Tooltip("Degrees per unit of Look: the mouse reports 0.1 per pixel")]
    public float Sensitivity = 1.0f;
    [Tooltip("Degrees per second with a stick fully over")]
    public float StickTurnRate = 160.0f;
    public bool InvertY = false;
    public float WalkSpeed = 5.0f;
    public float SprintSpeed = 9.0f;
    public float MaxHealth = 100.0f;
    [Tooltip("How far away a door or other interactable can be used from")]
    public float InteractRange = 2.5f;

    public Health Health { get; private set; } = new Health();
    public Inventory Inventory { get; } = new Inventory();
    private readonly Interactor interactor = new Interactor { Model = "hud" };
    private Vector3 start;

    private CharacterController character;
    private Transform head;
    private float pitch;

    public override void OnStart()
    {
        character = GetComponent<CharacterController>();
        Entity camera = World.Find(CameraName);
        head = camera ? camera.Transform : null;
        pitch = head != null ? head.Rotation.x : 0f;
        Input.MouseCaptured = true;
        if (Health.Max != MaxHealth)
        {
            Health = new Health(MaxHealth);
        }
        start = transform.WorldPosition;
        interactor.Range = InteractRange;
        UI.Model("hud").Set("health", (double)Health.Current);
    }

    public override void OnReload()
    {
        OnStart();
    }

    public override void OnDestroy()
    {
        Input.MouseCaptured = false;
    }

    public override void OnUpdate(float deltaTime)
    {
        if (character == null)
        {
            return;
        }

        if (Input.WasActionPressed("Pause"))
        {
            Input.MouseCaptured = false;
        }
        else if (!Input.MouseCaptured && Input.WasActionPressed("Fire"))
        {
            Input.MouseCaptured = true;
        }

        if (Input.MouseCaptured)
        {
            // The mouse reports how far it moved this frame; a stick reports how far it is pushed,
            // so it turns at a rate. Look is +Y up; positive pitch looks down.
            Vector2 look = Input.GetActionVector2("Look") * Sensitivity;
            if (Input.MouseDelta.Length() == 0f)
            {
                look = look * (StickTurnRate * deltaTime);
            }
            transform.Rotate(new Vector3(0f, look.x, 0f));
            pitch = Mathf.Clamp(pitch + (InvertY ? look.y : -look.y), -85f, 85f);
            if (head != null)
            {
                head.Rotation = new Vector3(pitch, 0f, 0f);
            }
        }

        Vector2 move = Input.GetActionVector2("Move");
        Vector3 forward = new Vector3(transform.Forward.x, 0f, transform.Forward.z).Normalized();
        Vector3 right = new Vector3(transform.Right.x, 0f, transform.Right.z).Normalized();
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

        interactor.Update(Entity, forward);
        if (Health.IsDead)
        {
            Checkpoint.Respawn(Entity, start);
        }
        UI.Model("hud").Set("health", (double)System.MathF.Ceiling(Health.Current));
    }
}
