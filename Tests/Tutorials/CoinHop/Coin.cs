using ScriptCore;

// A collectible: the player touching its trigger collects it.
public class Coin : Script
{
    public int Value = 1;

    public override void OnTriggerEnter(Entity other)
    {
        if (other != World.Find("Player")) return;
        EventBus.Publish(new CoinCollected(Value));
        Entity.Destroy();
    }
}

public readonly record struct CoinCollected(int Value);
