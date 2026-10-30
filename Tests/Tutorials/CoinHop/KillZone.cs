using ScriptCore;

// Falling into it puts the player back at the start.
public class KillZone : Script
{
    public Vector2 RespawnAt = new Vector2(0f, 1f);

    public override void OnTriggerEnter(Entity other)
    {
        if (other != World.Find("Player")) return;
        other.GetComponent<CharacterController2D>().Teleport(RespawnAt);
    }
}
