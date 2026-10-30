using ScriptCore;

namespace ExampleMod;

// MOD-2's test mod: a script class named like the game's Spinner, in the mod's own assembly. A scene
// names it "ExampleMod.Spinner" (the game's stays "Spinner"); both run side by side.
public class Spinner : Script
{
    private int frames;
    private float gameStart;

    public override void OnStart()
    {
        Debug.Log("ExampleMod.Spinner: started");
        var other = World.Find("GameSpinner");
        gameStart = other ? other.GetComponent<Transform>().Rotation.y : 0f;
    }

    public override void OnUpdate(float deltaTime)
    {
        transform.Rotate(new Vector3(0f, -90f * deltaTime, 0f));
        if (++frames == 60)
        {
            // The game's Spinner on the other entity has been turning it all along.
            var other = World.Find("GameSpinner");
            float turned = other ? other.GetComponent<Transform>().Rotation.y - gameStart : 0f;
            Debug.Log(System.Math.Abs(turned) > 1f ? "ExampleMod.Spinner: the game's Spinner turns its entity too" : $"ExampleMod.Spinner: the game's Spinner didn't turn ({turned})");
        }
    }
}
