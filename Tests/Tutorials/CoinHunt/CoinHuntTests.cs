using System.Threading.Tasks;
using ScriptCore;
using ScriptCore.Kit;

public static class CoinHuntTests
{
    private static void MoveTo(Entity target)
    {
        var player = World.Find("Player");
        player.GetComponent<CharacterController>().Teleport(target.GetComponent<Transform>().Position, new Vector3(0f, 0f, 1f));
    }

    [GameTest("Assets/Scenes/Main.lvl")]
    public static async Task Collecting_every_coin_wins()
    {
        bool won = false;
        using var subscription = EventBus.Subscribe<GoalReached>(_ => won = true);
        var inventory = World.Find("Player").GetScript<IInventory>()!.Inventory;
        for (int i = 1; i <= 5; ++i)
        {
            MoveTo(World.Find($"Coin {i}"));
            await GameAssert.Eventually(() => inventory.Count("Coin") == i, 2f, $"picked up coin {i}");
        }
        MoveTo(World.Find("Goal"));
        await GameAssert.Eventually(() => won, 2f, "reached the goal");
    }
}
