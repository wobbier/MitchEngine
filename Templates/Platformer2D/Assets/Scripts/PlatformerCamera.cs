using ScriptCore;

// A side-on camera that eases after its target: it leads the way the target runs by LookAhead,
// sits Height above it, and only follows vertically once the target leaves a band of VerticalSlack
// around it (so small hops don't bob the view).
public class PlatformerCamera : Script
{
    public string Target = "Player";
    public float LookAhead = 2.0f;
    public float Height = 1.5f;
    public float VerticalSlack = 1.0f;
    [Tooltip("Seconds to close half the distance to where the camera wants to be")]
    public float FollowHalfLife = 0.15f;

    private Entity target;
    private float lead;
    private float focusY;

    public override void OnStart()
    {
        target = World.Find(Target);
        if (target)
        {
            // Start in place rather than easing in from wherever the scene left the camera.
            Vector3 position = target.Transform.WorldPosition;
            focusY = position.y;
            lead = 0f;
            transform.WorldPosition = new Vector3(position.x, position.y + Height, transform.WorldPosition.z);
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
        Vector3 position = target.Transform.WorldPosition;
        float run = Input.GetActionVector2("Move").x;
        if (Mathf.Abs(run) > 0.1f)
        {
            lead = Mathf.Damp(lead, run > 0f ? LookAhead : -LookAhead, 0.3f, deltaTime);
        }
        if (position.y > focusY + VerticalSlack)
        {
            focusY = position.y - VerticalSlack;
        }
        else if (position.y < focusY - VerticalSlack)
        {
            focusY = position.y + VerticalSlack;
        }
        Vector3 desired = new Vector3(position.x + lead, focusY + Height, transform.WorldPosition.z);
        transform.WorldPosition = Mathf.Damp(transform.WorldPosition, desired, FollowHalfLife, deltaTime);
    }
}
