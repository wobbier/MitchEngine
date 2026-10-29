using System;

namespace ScriptCore;

// Entity queries (CS-5 h) and application calls (CS-5 j).
public static unsafe class Query
{
    // Every live entity with that component (any registered component name: "Light", "Rigidbody"...).
    public static Entity[] WithComponent(string component)
    {
        var name = Engine.Utf8(component);
        return Fill((buffer, max) => { fixed (byte* n = name) fixed (Entity* b = buffer) return Engine._api.World_FindAllWithComponent(n, b, max); });
    }

    // Every live entity with that name (World.Find returns one).
    public static Entity[] Named(string entityName)
    {
        var name = Engine.Utf8(entityName);
        return Fill((buffer, max) => { fixed (byte* n = name) fixed (Entity* b = buffer) return Engine._api.World_FindAllByName(n, b, max); });
    }

    internal delegate int Filler(Entity[] buffer, int max);

    // Calls the engine with a buffer, growing it when it reports more.
    internal static Entity[] Fill(Filler fill)
    {
        var buffer = new Entity[64];
        int total = fill(buffer, buffer.Length);
        if (total > buffer.Length)
        {
            buffer = new Entity[total];
            total = fill(buffer, buffer.Length);
        }
        Array.Resize(ref buffer, Math.Clamp(total, 0, buffer.Length));
        return buffer;
    }
}

public enum WindowMode { Windowed, Borderless, Fullscreen }

public static unsafe class App
{
    // Leaves the game (ignored in the editor).
    public static void Quit() => Engine._api.App_Quit();

    public static Vector2 WindowSize
    {
        get { Vector2 size; Engine._api.Window_GetSize(&size); return size; }
    }

    // The window mode (changing it is ignored in the editor; PLAY-1's Alt+Enter does the same).
    public static WindowMode WindowMode
    {
        get => (WindowMode)Engine._api.Window_GetMode();
        set => Engine._api.Window_SetMode((int)value);
    }

    // Rumbles a local player's pad (0..1 motors for `seconds`); false when the player has none.
    public static bool Rumble(int player, float low, float high, float seconds) => Engine._api.Input_Rumble(player, low, high, seconds) != 0;
}
