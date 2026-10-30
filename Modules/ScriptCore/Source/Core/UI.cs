using System.Text;

namespace ScriptCore;

/// <summary>
/// Script-owned view models for RmlUi documents (UI-3). Values live engine-side, so they
/// survive C# hot reloads; a document with data-model="name" binds to them and reloads once
/// when the model first appears. Typical shape:
///
/// <code>
///    var hud = UI.Model("hud");             // creates (or finds) the model
///    hud.Set("score", 0);                   // bound in RML as {{ score }}
///    hud.On("increment");                   //   &lt;button data-event-click="increment"&gt;
///    ...
///    for (string e; (e = hud.PollEvent()) != ""; )   // drain queued clicks each update
///        if (e == "increment") hud.Set("score", hud.GetNumber("score") + 1);
/// </code>
/// </summary>
public static class UI
{
    /// <summary>The view model of that name, created on first use (documents bind it with data-model).</summary>
    public static UIModel Model(string name)
    {
        unsafe { fixed (byte* m = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize])) Engine._api.UIModel_Ensure(m); }
        return new UIModel(name);
    }

    /// <summary>
    /// The UIDocument on the entity with this name (a menu such as the stock options menu,
    /// Assets/UI/Options/Options.rml). False when no UIDocument entity has that name.
    /// </summary>
    public static bool Show(string entityName) => SetVisible(entityName, 1);
    /// <summary>Hides a UIDocument entity's document; false when there's no such entity.</summary>
    public static bool Hide(string entityName) => SetVisible(entityName, 0);
    /// <summary>Shows the document if hidden, hides it if shown.</summary>
    public static bool Toggle(string entityName) => SetVisible(entityName, 2);

    /// <summary>Whether a UIDocument entity's document is showing.</summary>
    public static bool IsVisible(string entityName)
    {
        unsafe { fixed (byte* n = Engine.Utf8(entityName, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.UI_IsDocumentVisible(n) != 0; }
    }

    private static bool SetVisible(string entityName, int mode)
    {
        unsafe { fixed (byte* n = Engine.Utf8(entityName, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.UI_SetDocumentVisible(n, mode) != 0; }
    }
}

/// <summary>A view model: values RML binds ({{ key }}) and events it raises.</summary>
public readonly struct UIModel
{
    /// <summary>The model's name (data-model="name").</summary>
    public readonly string Name;

    internal UIModel(string name) { Name = name; }

    /// <summary>Sets a number.</summary>
    public void Set(string key, double value)
    {
        unsafe { fixed (byte* m = Engine.Utf8(Name, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(key, stackalloc byte[Engine.Utf8StackSize])) Engine._api.UIModel_SetNumber(m, k, value); }
    }

    /// <summary>Sets a bool (stored as 1 or 0).</summary>
    public void Set(string key, bool value) => Set(key, value ? 1.0 : 0.0);

    /// <summary>Sets a string.</summary>
    public void Set(string key, string value)
    {
        unsafe { fixed (byte* m = Engine.Utf8(Name, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(key, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* v = Engine.Utf8(value ?? "", stackalloc byte[Engine.Utf8StackSize])) Engine._api.UIModel_SetString(m, k, v); }
    }

    /// <summary>
    /// Registers a data-event name the document can raise (data-event-click="name").
    /// </summary>
    public void On(string eventName)
    {
        unsafe { fixed (byte* m = Engine.Utf8(Name, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(eventName, stackalloc byte[Engine.Utf8StackSize])) Engine._api.UIModel_RegisterEvent(m, k); }
    }

    /// <summary>A number's value (0 when unset).</summary>
    public double GetNumber(string key)
    {
        unsafe { fixed (byte* m = Engine.Utf8(Name, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(key, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.UIModel_GetNumber(m, k); }
    }

    /// <summary>A string's value ("" when unset).</summary>
    public string GetString(string key)
    {
        unsafe
        {
            var buffer = new byte[512];
            int length;
            fixed (byte* m = Engine.Utf8(Name, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(key, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* b = buffer)
                length = Engine._api.UIModel_GetString(m, k, b, buffer.Length);
            return Encoding.UTF8.GetString(buffer, 0, System.Math.Clamp(length, 0, buffer.Length));
        }
    }

    /// <summary>
    /// The oldest queued event name, or "" when none are waiting.
    /// </summary>
    public string PollEvent()
    {
        unsafe
        {
            var buffer = new byte[256];
            int length;
            fixed (byte* m = Engine.Utf8(Name, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* b = buffer)
                length = Engine._api.UIModel_PollEvent(m, b, buffer.Length);
            return length > 0 ? Encoding.UTF8.GetString(buffer, 0, System.Math.Min(length, buffer.Length)) : "";
        }
    }
}
