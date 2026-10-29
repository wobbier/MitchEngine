using ScriptCore;

// A high camera that eases after its target at a fixed angle: Pitch tilts it down from the
// horizon, Yaw turns the whole view around the target, Distance sets how far back it sits.
public class TopDownCamera : Script
{
    public string Target = "Player";
    public float Distance = 16.0f;
    [Range(10f, 89f)]
    public float Pitch = 55.0f;
    public float Yaw = 0.0f;
    [Tooltip("Seconds to close half the distance to where the camera wants to be")]
    public float FollowHalfLife = 0.12f;

    private Entity target;

    public override void OnStart()
    {
        target = World.Find(Target);
        if (target)
        {
            // Start in place rather than easing in from wherever the scene left the camera.
            transform.WorldPosition = Desired(out Vector3 focus);
            transform.LookAt(focus);
        }
    }

    public override void OnReload()
    {
        OnStart();
    }

    public override void OnLateUpdate(float deltaTime)
    {
        if (!target)
        {
            return;
        }
        Vector3 desired = Desired(out Vector3 focus);
        transform.WorldPosition = Mathf.Damp(transform.WorldPosition, desired, FollowHalfLife, deltaTime);
        transform.LookAt(transform.WorldPosition + (focus - desired));
    }

    // Where the camera wants to be for the target's current position, and the point it looks at.
    private Vector3 Desired(out Vector3 focus)
    {
        float yaw = Yaw * Mathf.Deg2Rad;
        float pitch = Pitch * Mathf.Deg2Rad;
        Vector3 forward = new Vector3(Mathf.Sin(yaw) * Mathf.Cos(pitch), -Mathf.Sin(pitch), Mathf.Cos(yaw) * Mathf.Cos(pitch));
        focus = target.Transform.WorldPosition;
        return focus - forward * Distance;
    }
}
