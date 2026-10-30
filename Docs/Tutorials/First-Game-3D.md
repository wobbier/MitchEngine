# Make your first game: Coin Hunt (3D)

In about half an hour you'll turn the **First-Person** template into a small game: five coins
hidden around the course, a counter on the HUD, and a finish pad that only lets you win once
you've found them all. Then you'll write a play-mode test for it and build a shippable copy.

You'll use the editor (Havana), the C# starter kit that ships with the engine, one new script
and a few lines of HUD markup. No C++.

> Every snippet on this page is checked: the engine's tutorial check builds this exact game from
> the template and runs the test at the end (see "How this page is tested").

## 1. Make the project

Start Havana with no project and the **Project Hub** opens. Choose **New Project**, pick
**First-Person**, name it `CoinHunt` and pick a folder. The Hub creates the project and opens it.

From a terminal, the same thing is:

```bash
Havana --new-project FirstPerson --name CoinHunt --in ~/Games
Havana --project ~/Games/CoinHunt
```

The project is plain folders: `Assets/Scenes/Main.lvl` is the level, `Assets/Scripts/` holds the
C# scripts, `Assets/UI/Hud.rml` the HUD, and `Project/Game.meproj` marks the folder as a project.

## 2. Play what's there

Press **Play** (the triangle in the menu bar). Walk with **WASD**, look with the mouse, sprint
with **Shift**, jump with **Space**, interact with **E**, and press **Escape** for the pause menu.
Stop with the square button; the scene goes back to how it was.

The course already uses the starter kit (`ScriptCore.Kit`, see
[Scripting-DotNet](../Scripting-DotNet.md#starter-kit-game-7)):

| Entity | Script | What it does |
|---|---|---|
| Player | `FirstPersonController` (the template's) | Moves the `CharacterController`, owns `Health` and an `Inventory`, shows the HUD |
| Key | `Pickup` (Item `Key`) | Adds a key to whoever walks into its trigger |
| Gate | `Door` (Required Item `Key`) | Opens with **E** once you carry the key |
| Hazard | `DamageZone` | Hurts while you stand in it |
| Medkit | `Pickup` (Heal 50) | Heals you, if you're hurt |
| Checkpoint | `Checkpoint` | Where you respawn when health runs out |

Select any of them and look at the **Properties** panel: the script's public fields are editable
there, and the trigger is a `BoxCollider` with **Is Trigger** ticked.

## 3. Place the coins

A coin is just another `Pickup`, with a different item name.

1. Select **Key** in the Hierarchy and press **Ctrl+D** to duplicate it. Rename the copy
   (**F2**) to `Coin 1`.
2. In Properties, set its **Pickup** script's **Item** to `Coin`. Give it a gold material colour
   and a coin-like scale, such as `0.2, 0.2, 0.05`. Its trigger scales with it, so make the
   **BoxCollider Size** a little larger (`4, 4, 16`) to keep it easy to grab.
3. Move it somewhere worth walking to with the move gizmo (**W**).
4. Duplicate it four more times (`Coin 2` to `Coin 5`) and spread them around: one on the
   stair landing, one on the high platform, one behind the start.
5. **Ctrl+S** saves the scene.

Play again and walk through a coin: it vanishes into your inventory. Nothing shows it yet; that's
the next step.

## 4. The finish pad and the coin counter

Add an entity for the goal: in the Hierarchy choose **Create > 3D Object > Cube**, name it
`Goal`, scale it flat (`1, 0.05, 1`), give it a green material and put it somewhere near the
start. The cube comes with a **BoxCollider**: tick **Is Trigger** and make its **Size** tall
(`2, 40, 2`), so it catches anyone standing on the pad.

Now the game's one script. In the Asset Browser, open `Assets/Scripts`, right-click and choose
**Create > C# Script > Script**, rename the new file to `Goal.cs` (**F2**), open it and replace
its contents with:

```csharp
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
```

What it does:

- **`OnStart`** finds the player by name and clears the HUD message.
- **`OnUpdate`** writes the coin count into the HUD's data model each frame. `UI.Model("hud")` is
  the same model `FirstPersonController` fills with `health`; any script can add values to it.
- **`OnTriggerEnter`** runs when something enters the goal's trigger. It checks the player's
  inventory (the `IInventory` the controller implements) and either says how many coins are
  missing or declares the win.
- **`GoalReached`** is a plain record published on the `EventBus`, so other scripts (a fanfare,
  a sequence, the test below) can react with `On<GoalReached>(...)`.

Save the file: the editor rebuilds the scripts in the background (the title bar shows it), and
compile errors open in the **Error List**. Select the **Goal** entity, click **Add Component**,
type `Script` and choose **ScriptComponent**, then pick `Goal` in its **Script** dropdown (an
entity holds one script). **Coins Needed** shows up as an editable field.

## 5. Show it on the HUD

Open `Assets/UI/Hud.rml` (double-click it, or edit it in your text editor; the game reloads it
while playing). Add two lines inside `<body>`, after the prompt:

```html
<div id="coins">Coins {{ coins }} / {{ needed }}</div>
<div id="message">{{ message }}</div>
```

and give them a place on screen in the `<style>` block:

```css
#coins {
    position: absolute;
    right: 24dp;
    top: 20dp;
    padding: 6dp 12dp;
    background-color: #07090DB0;
    border-radius: 4dp;
}
#message {
    position: absolute;
    left: 0dp;
    right: 0dp;
    top: 30%;
    text-align: center;
    font-size: 36dp;
}
```

`{{ coins }}` reads the `coins` value your script sets. Press Play: the counter sits in the top
right, ticks up as you collect coins, and the pad tells you how many you're missing until you
have them all. Then it says **You win!**

## 6. Test it

Play-testing by hand gets old fast. A **game test** plays the level for you. Create
`Assets/Scripts/Tests/CoinHuntTests.cs`:

```csharp
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
```

`[GameTest("Assets/Scenes/Main.lvl")]` loads the level fresh, then the test teleports the player
onto each coin and waits (in game time) for the inventory to count it, then onto the goal, and
waits for the `GoalReached` event. `GameAssert.Eventually` fails the test with its message if the
condition isn't true in time.

Run it: press Play, open the console with **~** and type `game-tests`. The log shows

```
[GameTest] PASS CoinHuntTests.Collecting_every_coin_wins (0.5 s)
GameTests: 4 passed, 0 failed
```

(the other three are the template's own tests, in `Assets/Scripts/Tests/FirstPersonTests.cs`).
`game-tests Coin` runs just yours. To run them unattended, as a build machine would, put these
lines in an `.edscript` and pass it with `--editor-exec`:

```
play
wait 30
console game-tests
wait-log GameTests: 4 passed, 0 failed | 3000
stop
```

```bash
Havana --project ~/Games/CoinHunt --editor-exec Tests.edscript
```

The editor exits non-zero when the flow fails. See [Play-mode tests](../Scripting-DotNet.md#play-mode-tests-cs-12).

## 7. Ship it

**File > Build Game** cooks only what `Main.lvl` uses, compiles the scripts in Release, and puts
a runnable folder under `Builds/`, with its own copy of .NET inside, so players don't need to
install anything. Run the launcher in that folder. The same from a terminal:

```bash
Havana --project ~/Games/CoinHunt --build-game
```

See [Building](../Building.md) for profiles and what goes in the folder.

## Where next

- Make coins **respawn** with a `Spawner`, or a **timer** with `Timer.Every` and a `time` HUD value.
- Open the Gate with a **cutscene**: a [Sequence](../Sequences.md) with a camera cut to the gate
  and an event marker your script receives through `ISequenceReceiver`.
- Give the finish pad a **conversation** first: [Dialogue](../Scripting-DotNet.md#dialogue-game-8).
- Add a guard that patrols and chases you: the AI kit's `GuardBrain` ([AI](../Scripting-DotNet.md#ai-game-3)).
- The 2D version of this tutorial: [Coin Hop](First-Game-2D.md).

## How this page is tested

`Engine/Tests/Tutorials/CoinHunt/` holds this page's `Goal.cs`, `CoinHuntTests.cs`, a script that
places the coins, the goal and the HUD lines in a fresh First-Person project, and the flow above.
RunChecks' Tutorials step creates the project, applies them and requires `GameTests: 4 passed`.
If you change a snippet here, change it there.
