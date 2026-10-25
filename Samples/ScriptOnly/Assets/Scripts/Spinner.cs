using ScriptCore;

// Turns its entity about Y and bobs it up and down: the sample's one piece of game code.
public class Spinner : Script
{
    public float DegreesPerSecond = 30.0f;
    public float BobHeight = 0.25f;

    private float m_baseHeight;
    private float m_time;

    public override void OnStart()
    {
        m_baseHeight = transform.Position.y;
        Debug.Log($"Spinner started on {Entity.Name}");
    }

    public override void OnUpdate(float deltaTime)
    {
        m_time += deltaTime;
        transform.Rotate(new Vector3(0, DegreesPerSecond * deltaTime, 0));
        var position = transform.Position;
        transform.Position = new Vector3(position.x, m_baseHeight + BobHeight * Mathf.Sin(m_time * 2.0f), position.z);
    }
}
