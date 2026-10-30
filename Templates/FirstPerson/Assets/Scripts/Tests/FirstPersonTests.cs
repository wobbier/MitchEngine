using System.Threading.Tasks;
using ScriptCore;
using ScriptCore.Kit;

// Play-mode tests for this template (CS-12). Run them in Play with the console: game-tests
// Each loads Main.lvl fresh, moves the player (with Teleport, or held keys through SimulatedInput)
// and checks what the kit pieces did.
public static class FirstPersonTests
{
    private const string Scene = "Assets/Scenes/Main.lvl";

    private static Entity Player() => World.Find("Player");

    private static void MoveTo(Vector3 position) => Player().GetComponent<CharacterController>().Teleport(position, new Vector3(0f, 0f, 1f));

    [GameTest(Scene)]
    public static async Task Player_starts_healthy_on_the_ground()
    {
        var health = Player().GetScript<IHealth>();
        GameAssert.True(health != null, "the player has health");
        GameAssert.Equal(health!.Health.Max, health.Health.Current, "health");
        await GameAssert.Eventually(() => Player().GetComponent<Transform>().Position.y < 1.2f, 3f, "the player settles");
    }

    [GameTest(Scene)]
    public static async Task Holding_W_walks_the_player_forward()
    {
        var body = Player().GetComponent<Transform>();
        await GameAssert.Eventually(() => body.Position.y < 1.2f, 3f, "the player settles");
        var start = body.Position;
        await SimulatedInput.Hold(KeyCode.W, 1f);
        GameAssert.True(body.Position.z > start.z + 1f, $"walked forward: z {start.z} -> {body.Position.z}");
        GameAssert.Near(start.x, body.Position.x, 0.25f, "x");
    }

    [GameTest(Scene)]
    public static async Task Picking_up_the_key_adds_it_to_the_inventory()
    {
        var inventory = Player().GetScript<IInventory>()!.Inventory;
        GameAssert.True(!inventory.Has("Key"), "no key at the start");
        MoveTo(new Vector3(0f, 1f, -1.5f));
        await GameAssert.Eventually(() => inventory.Has("Key"), 3f, "picked up the key");
    }

    [GameTest(Scene)]
    public static async Task The_hazard_hurts_and_the_medkit_heals()
    {
        var health = Player().GetScript<IHealth>()!.Health;
        MoveTo(new Vector3(0f, 1f, 6.25f));
        await GameAssert.Eventually(() => health.Current < health.Max, 3f, "the hazard hurt");
        MoveTo(new Vector3(3f, 1f, -2f));
        float hurt = health.Current;
        await GameAssert.Eventually(() => health.Current > hurt, 3f, "the medkit healed");
    }
}
