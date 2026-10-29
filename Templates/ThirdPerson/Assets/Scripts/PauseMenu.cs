using ScriptCore;

// Pause (Escape / Start) opens and closes the stock options menu: the UIDocument on the entity named
// in MenuName, showing Assets/UI/Options/Options.rml (the engine ships it; ship your own file of that
// name to replace it). While it's open the game is paused and the cursor is free, and its Back
// button closes it. Graphics, volumes, rebinds and accessibility are saved for the player
// (Settings).
[ExecutionOrder(100)]
public class PauseMenu : Script
{
    public string MenuName = "Options";

    private bool wasOpen;

    public override void OnUpdate(float deltaTime)
    {
        if (Input.WasActionPressed("Pause"))
        {
            UI.Toggle(MenuName);
        }

        bool open = UI.IsVisible(MenuName);
        if (open)
        {
            // Runs after the game's scripts (ExecutionOrder), so a click on the menu can't
            // recapture the mouse.
            Input.MouseCaptured = false;
        }
        if (open != wasOpen)
        {
            Time.TimeScale = open ? 0f : 1f;
            wasOpen = open;
        }
    }

    public override void OnDestroy()
    {
        if (wasOpen)
        {
            Time.TimeScale = 1f;
        }
    }
}
