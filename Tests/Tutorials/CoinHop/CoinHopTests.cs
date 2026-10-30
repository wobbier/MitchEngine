using System.Threading.Tasks;
using ScriptCore;

public static class CoinHopTests
{
    private static CharacterController2D Body() => World.Find("Player").GetComponent<CharacterController2D>();

    [GameTest("Assets/Scenes/Main.lvl")]
    public static async Task Touching_a_coin_counts_it()
    {
        var counter = World.Find("Hud").GetScript<CoinCounter>()!;
        var coin = World.Find("Coin 1").GetComponent<Transform>().Position;
        Body().Teleport(new Vector2(coin.x, coin.y));
        await GameAssert.Eventually(() => counter.Coins == 1, 2f, "collected a coin");
        GameAssert.True(!World.Find("Coin 1").IsAlive, "the coin is gone");
    }

    [GameTest("Assets/Scenes/Main.lvl")]
    public static async Task Falling_off_respawns_at_the_start()
    {
        Body().Teleport(new Vector2(3f, -6f));
        await GameAssert.Eventually(() => World.Find("Player").GetComponent<Transform>().Position.y > 0f, 3f, "back at the start");
    }
}
