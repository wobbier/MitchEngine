using ScriptCore;
using ScriptCore.Kit;

// The finish line: shows the coin count on the HUD and wins once the player arrives with enough.
public class Goal : Script
{
    public int CoinsNeeded = 5;

    private Entity _player;
    private bool _won;

    public override void OnStart()
    {
        _player = World.Find("Player");
        UI.Model("hud").Set("message", "");
    }

    public override void OnUpdate(float deltaTime)
    {
        var hud = UI.Model("hud");
        hud.Set("coins", Coins());
        hud.Set("needed", CoinsNeeded);
    }

    public override void OnTriggerEnter(Entity other)
    {
        if (_won || other != _player) return;
        int missing = CoinsNeeded - Coins();
        if (missing > 0)
        {
            UI.Model("hud").Set("message", $"Find {missing} more coin{(missing == 1 ? "" : "s")}!");
            return;
        }
        _won = true;
        UI.Model("hud").Set("message", "You win!");
        EventBus.Publish(new GoalReached(Coins()));
    }

    private int Coins() => _player.GetScript<IInventory>()?.Inventory.Count("Coin") ?? 0;
}

// Anyone can listen for the win: On<GoalReached>(e => ...) in a script.
public readonly record struct GoalReached(int Coins);
