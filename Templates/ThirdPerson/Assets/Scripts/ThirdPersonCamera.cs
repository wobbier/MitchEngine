using ScriptCore;

// Orbiting third-person camera: Look turns it around the target, it eases after the target, and it
// pulls in when geometry is between the target and the camera. Pause (Escape / Start) frees the
// cursor, Fire captures it again.
public class ThirdPersonCamera : Script
{
    public string Target = "Player";
    public float Distance = 6.0f;
    [Tooltip("Height of the point the camera orbits, above the target's origin")]
    public float Height = 1.4f;
    public float Yaw = 0.0f;
    public float Pitch = 18.0f;
    [Tooltip("Degrees per unit of Look: the mouse reports 0.1 per pixel")]
    public float Sensitivity = 1.5f;
    [Tooltip("Degrees per second with a stick fully over")]
    public float StickTurnRate = 160.0f;
    public bool InvertY = false;
    [Tooltip("Seconds to close half the distance to where the camera wants to be")]
    public float FollowHalfLife = 0.05f;

    private Entity target;

    public override void OnStart()
    {
        target = World.Find(Target);
        Input.MouseCaptured = true;
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
        if (!target)
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
            // so it turns at a rate. Look is +Y up.
            Vector2 look = Input.GetActionVector2("Look") * Sensitivity;
            if (Input.MouseDelta.Length() == 0f)
            {
                look = look * (StickTurnRate * deltaTime);
            }
            Yaw += look.x;
            Pitch = Mathf.Clamp(Pitch + (InvertY ? look.y : -look.y), -10f, 70f);
        }

        float yaw = Yaw * Mathf.Deg2Rad;
        float pitch = Pitch * Mathf.Deg2Rad;
        Vector3 forward = new Vector3(Mathf.Sin(yaw) * Mathf.Cos(pitch), -Mathf.Sin(pitch), Mathf.Cos(yaw) * Mathf.Cos(pitch));
        Vector3 pivot = target.Transform.WorldPosition + Vector3.Up * Height;

        // Keep a little clear of walls between the pivot and the camera.
        float distance = Distance;
        if (Physics.Raycast(pivot, -forward, Distance, out var hit) && hit.Entity != target)
        {
            distance = Mathf.Max(hit.Distance - 0.3f, 0.5f);
        }
        Vector3 desired = pivot - forward * distance;
        transform.WorldPosition = Mathf.Damp(transform.WorldPosition, desired, FollowHalfLife, deltaTime);
        transform.LookAt(pivot);
    }
}
