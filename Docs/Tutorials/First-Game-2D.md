# Make your first game: Coin Hop (2D)

The **2D Platformer** template gives you a running, jumping hero on a tilemap level, with five
coins hanging in the air that do nothing yet. In this tutorial you'll make them collectible, count
them on a HUD, catch the player when they fall off the world, test it all automatically, and
build the game. It takes about twenty minutes.

It's the 2D twin of [Coin Hunt](First-Game-3D.md); do either first.

> Every snippet on this page is checked: the engine's tutorial check builds this exact game from
> the template and runs its tests (see "How this page is tested").

## 1. Make the project and play it

Start Havana with no project to get the **Project Hub**, choose **New Project > 2D Platformer**
and call it `CoinHop`. From a terminal:

```bash
Havana --new-project Platformer2D --name CoinHop --in ~/Games
Havana --project ~/Games/CoinHop
```

Press **Play**. **A/D** (or a gamepad's left stick) run, **Left Shift** sprints and
**Space** jumps; the jump is forgiving: you can still jump just after running off a ledge, and a
press just before landing is remembered.

What's in `Assets/Scenes/Main.lvl`:

| Entity | What it is |
|---|---|
| Level | A `Tilemap` painted from `Tiles.spritesheet`, with a `TilemapCollider2D` that turns its solid tiles into collision |
| Player | A `SpriteRenderer` + `Animator` (Idle and Walk flipbooks), a `CharacterController2D`, and `PlatformerController.cs` |
| Main Camera | An orthographic camera with `PlatformerCamera.cs`, which leads the way you run |
| Coin 1-5 | Just sprites, for now |

2D physics runs in the XY plane (Box2D), so positions here are `x` right, `y` up.

## 2. Make the coins collectible

A coin needs a trigger to notice the player, and a script to react. In the Asset Browser, open
`Assets/Scripts`, right-click and choose **Create > C# Script > Script**, rename it `Coin.cs`
(**F2**) and give it:

```csharp
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
```

`OnTriggerEnter` runs when another body enters this entity's trigger. The coin doesn't count
anything itself: it publishes a `CoinCollected` event on the `EventBus` and removes itself, so
whatever wants to know (a counter, a sound, an achievement) can listen.

Now wire up **Coin 1**: select it, **Add Component > CircleCollider2D**, set its **Radius** to
`0.4` and tick **Is Trigger**; then **Add Component > ScriptComponent** and pick `Coin` in its
**Script** dropdown. Do the same for the other four (or set up one, delete the others and
duplicate it with **Ctrl+D**).

Play and run into a coin: it disappears.

## 3. Count them on a HUD

The HUD is an [RmlUi](../UI-RmlUi-and-ImGui.md) document: HTML-like markup with CSS. Create a
folder `Assets/UI` (**Create > Folder**) and in it a file `Hud.rml` with your text editor:

```html
<rml>
<head>
	<title>HUD</title>
	<style>
		body {
			font-family: Inter;
			font-size: 20dp;
			color: #FFFFFF;
			width: 100%;
			height: 100%;
		}
		#coins {
			position: absolute;
			right: 24dp;
			top: 20dp;
			padding: 6dp 12dp;
			background-color: #00000090;
			border-radius: 4dp;
		}
	</style>
</head>
<body data-model="hud">
	<div id="coins">Coins {{ coins }}</div>
</body>
</rml>
```

`data-model="hud"` binds the page to a model named `hud`, and `{{ coins }}` shows its `coins`
value. A script fills it in. Create `CoinCounter.cs`:

```csharp
using ScriptCore;

// Counts collected coins and shows them on the HUD.
public class CoinCounter : Script
{
    public int Coins { get; private set; }

    public override void OnStart()
    {
        UI.Model("hud").Set("coins", 0);
        On<CoinCollected>(coin =>
        {
            Coins += coin.Value;
            UI.Model("hud").Set("coins", Coins);
        });
    }
}
```

`On<CoinCollected>(...)` subscribes for as long as the script lives (it's dropped automatically
when the script is destroyed), and `UI.Model("hud").Set(...)` updates the page. `Coins` is public
so other code, like the test below, can read it.

Then add the HUD to the scene: **Create > Empty Entity**, name it `Hud`, **Add Component >
UIDocument** with **File Path** `Assets/UI/Hud.rml`, and a **ScriptComponent** with
`CoinCounter`. Play: the counter sits top right and goes up with every coin.

## 4. Don't fall forever

Run off the end of the level and you fall forever. Add a floor that sends you back.
`KillZone.cs`:

```csharp
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
```

**Create > Physics 2D > Trigger Area**, name it `Kill Zone`, move it well below the level
(`14, -10`) and make its **BoxCollider2D Size** wide (`200, 4`). Add a ScriptComponent with
`KillZone`. **Respawn At** is a field you can change in the Properties panel.

## 5. Test it

`Assets/Scripts/Tests/CoinHopTests.cs`:

```csharp
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
```

Each `[GameTest]` loads `Main.lvl` fresh, moves the player with `Teleport` and waits for the
result. Press Play, open the console with **~** and run `game-tests`:

```
[GameTest] PASS CoinHopTests.Falling_off_respawns_at_the_start (0.7 s)
[GameTest] PASS CoinHopTests.Touching_a_coin_counts_it (0.1 s)
GameTests: 2 passed, 0 failed
```

The same tests run unattended from an `.edscript` (see the [3D tutorial](First-Game-3D.md#6-test-it)).

## 6. Ship it

**File > Build Game** (or `Havana --project ~/Games/CoinHop --build-game`) makes a folder under
`Builds/` with everything the game needs, .NET included. See [Building](../Building.md).

## Where next

- Paint more level: select **Level**, click **Paint** in its Tilemap inspector, and draw with
  the tile palette ([Rendering-Pipeline](../Rendering-Pipeline.md), Tilemaps).
- Animate the coins: a `.spriteanim` flipbook in `Coin`'s `Animator`.
- Add enemies that patrol between two points with `Tween.Move`, and a `KillZone` on each.
- A win flag at the end of the level, like the 3D tutorial's goal.

## How this page is tested

`Engine/Tests/Tutorials/CoinHop/` holds this page's scripts and `Hud.rml`, a script that adds the
colliders, the HUD and the kill zone to a fresh 2D Platformer project, and a flow that runs the
tests. RunChecks' Tutorials step requires `GameTests: 2 passed`. If you change a snippet here,
change it there: the check also fails when a file's text no longer appears on this page.
