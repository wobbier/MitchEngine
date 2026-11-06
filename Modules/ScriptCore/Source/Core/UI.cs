using System.Text;
using System.Text.Json;

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
///
///    hud.On("pick");                        //   &lt;button data-event-click="pick(2)"&gt;
///    while (hud.PollEvent(out UIEvent click))
///        if (click.Name == "pick") Pick(click.Int(0));
///
///    var rows = hud.List("players");        //   &lt;div data-for="row : players"&gt;{{ row.name }}&lt;/div&gt;
///    rows.Count = players.Count;
///    for (int i = 0; i &lt; players.Count; ++i) rows.Set(i, "name", players[i].Name);
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

    /// <summary>
    /// A list of records for data-for (ME-312): &lt;div data-for="row : players"&gt;{{ row.name }}&lt;/div&gt;.
    /// Use a key that isn't also a plain value.
    /// </summary>
    public UIList List(string key) => new UIList(Name, key);

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
    /// Pops the oldest queued event with its arguments (ME-312): data-event-click="pick(2, 'fast')"
    /// arrives as Name "pick" with Int(0) == 2 and String(1) == "fast". False when none are waiting.
    /// </summary>
    public bool PollEvent(out UIEvent uiEvent)
    {
        string name = PollEvent();
        if (name.Length == 0)
        {
            uiEvent = default;
            return false;
        }
        uiEvent = new UIEvent(name, ReadEventArgs());
        return true;
    }

    private JsonElement ReadEventArgs()
    {
        unsafe
        {
            int length;
            fixed (byte* m = Engine.Utf8(Name, stackalloc byte[Engine.Utf8StackSize])) length = Engine._api.UIModel_GetEventArgs(m, null, 0);
            if (length <= 2)
            {
                return default;   // "[]": no arguments
            }
            var buffer = new byte[length + 1];
            fixed (byte* m = Engine.Utf8(Name, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* b = buffer)
                length = Engine._api.UIModel_GetEventArgs(m, b, buffer.Length);
            try
            {
                using var document = JsonDocument.Parse(new System.ReadOnlyMemory<byte>(buffer, 0, System.Math.Clamp(length, 0, buffer.Length - 1)));
                return document.RootElement.ValueKind == JsonValueKind.Array ? document.RootElement.Clone() : default;
            }
            catch (JsonException)
            {
                return default;
            }
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

/// <summary>
/// A list in a view model (UIModel.List), bound with data-for="row : key": each item is a record of
/// number and string fields ({{ row.name }}, data-if="row.local", "key.size" for the count; it_index
/// is the row's index). Values live engine-side like the model's; only a change redraws, and any
/// change re-evaluates every row of the list.
/// </summary>
public readonly struct UIList
{
    /// <summary>The model's name.</summary>
    public readonly string Model;
    /// <summary>The list's key in the model.</summary>
    public readonly string Key;

    internal UIList(string model, string key)
    {
        Model = model;
        Key = key;
    }

    /// <summary>How many items it has. Setting it adds items without fields or drops them from the end.</summary>
    public int Count
    {
        get { unsafe { fixed (byte* m = Engine.Utf8(Model, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(Key, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.UIModel_GetListCount(m, k); } }
        set { unsafe { fixed (byte* m = Engine.Utf8(Model, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(Key, stackalloc byte[Engine.Utf8StackSize])) Engine._api.UIModel_SetListCount(m, k, value); } }
    }

    /// <summary>Sets item index's field to a number; an index past the end grows the list.</summary>
    public void Set(int index, string field, double value)
    {
        unsafe
        {
            fixed (byte* m = Engine.Utf8(Model, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(Key, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* f = Engine.Utf8(field, stackalloc byte[Engine.Utf8StackSize]))
                Engine._api.UIModel_SetItemNumber(m, k, index, f, value);
        }
    }

    /// <summary>Sets item index's field to a bool (stored as 1 or 0).</summary>
    public void Set(int index, string field, bool value) => Set(index, field, value ? 1.0 : 0.0);

    /// <summary>Sets item index's field to a string.</summary>
    public void Set(int index, string field, string value)
    {
        unsafe
        {
            fixed (byte* m = Engine.Utf8(Model, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* k = Engine.Utf8(Key, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* f = Engine.Utf8(field, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* v = Engine.Utf8(value ?? "", stackalloc byte[Engine.Utf8StackSize]))
                Engine._api.UIModel_SetItemString(m, k, index, f, v);
        }
    }
}

/// <summary>
/// A data event a document raised (UIModel.PollEvent): its name and the arguments RmlUi evaluated,
/// data-event-click="pick(2, 'fast', true)" giving Int(0) == 2, String(1) == "fast", Bool(2).
/// </summary>
public readonly struct UIEvent
{
    /// <summary>The event's name (data-event-click="name(...)").</summary>
    public readonly string Name;
    private readonly JsonElement args;

    internal UIEvent(string name, JsonElement arguments)
    {
        Name = name;
        args = arguments;
    }

    /// <summary>How many arguments it was raised with.</summary>
    public int ArgCount => args.ValueKind == JsonValueKind.Array ? args.GetArrayLength() : 0;

    /// <summary>Argument i as a number: a number, a numeric string, or a bool as 1 / 0; the fallback otherwise.</summary>
    public double Number(int i, double fallback = 0)
    {
        if (!TryGet(i, out var arg)) return fallback;
        switch (arg.ValueKind)
        {
            case JsonValueKind.Number: return arg.GetDouble();
            case JsonValueKind.True: return 1;
            case JsonValueKind.False: return 0;
            case JsonValueKind.String:
                return double.TryParse(arg.GetString(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double value) ? value : fallback;
            default: return fallback;
        }
    }

    /// <summary>Argument i as a whole number (Number, rounded); the fallback when it isn't one.</summary>
    public int Int(int i, int fallback = 0) => TryGet(i, out _) ? (int)System.Math.Round(Number(i, fallback)) : fallback;

    /// <summary>Argument i as text: a string as itself, a number or bool as written ("2", "true"); "" when missing.</summary>
    public string String(int i)
    {
        if (!TryGet(i, out var arg)) return "";
        return arg.ValueKind == JsonValueKind.String ? arg.GetString() ?? "" : arg.GetRawText();
    }

    /// <summary>Argument i as a bool: true, or a non-zero number; false otherwise.</summary>
    public bool Bool(int i) => TryGet(i, out var arg) && (arg.ValueKind == JsonValueKind.True || (arg.ValueKind == JsonValueKind.Number && arg.GetDouble() != 0));

    /// <summary>"name(arg, ...)".</summary>
    public override string ToString()
    {
        int count = ArgCount;
        if (count == 0) return Name ?? "";
        var text = new StringBuilder(Name).Append('(');
        for (int i = 0; i < count; ++i)
        {
            if (i > 0) text.Append(", ");
            text.Append(String(i));
        }
        return text.Append(')').ToString();
    }

    private bool TryGet(int i, out JsonElement arg)
    {
        if ((uint)i < (uint)ArgCount)
        {
            arg = args[i];
            return true;
        }
        arg = default;
        return false;
    }
}
