using System.Text;

namespace ScriptCore;

// Script-owned view models for RmlUi documents (UI-3). Values live engine-side, so they
// survive C# hot reloads; a document with data-model="name" binds to them and reloads once
// when the model first appears. Typical shape:
//
//   var hud = UI.Model("hud");             // creates (or finds) the model
//   hud.Set("score", 0);                   // bound in RML as {{ score }}
//   hud.On("increment");                   //   <button data-event-click="increment">
//   ...
//   for (string e; (e = hud.PollEvent()) != ""; )   // drain queued clicks each update
//       if (e == "increment") hud.Set("score", hud.GetNumber("score") + 1);
public static class UI
{
    public static UIModel Model(string name)
    {
        unsafe { fixed (byte* m = Engine.Utf8(name)) Engine._api.UIModel_Ensure(m); }
        return new UIModel(name);
    }
}

public readonly struct UIModel
{
    public readonly string Name;

    internal UIModel(string name) { Name = name; }

    public void Set(string key, double value)
    {
        unsafe { fixed (byte* m = Engine.Utf8(Name)) fixed (byte* k = Engine.Utf8(key)) Engine._api.UIModel_SetNumber(m, k, value); }
    }

    public void Set(string key, bool value) => Set(key, value ? 1.0 : 0.0);

    public void Set(string key, string value)
    {
        unsafe { fixed (byte* m = Engine.Utf8(Name)) fixed (byte* k = Engine.Utf8(key)) fixed (byte* v = Engine.Utf8(value ?? "")) Engine._api.UIModel_SetString(m, k, v); }
    }

    // Registers a data-event name the document can raise (data-event-click="name").
    public void On(string eventName)
    {
        unsafe { fixed (byte* m = Engine.Utf8(Name)) fixed (byte* k = Engine.Utf8(eventName)) Engine._api.UIModel_RegisterEvent(m, k); }
    }

    public double GetNumber(string key)
    {
        unsafe { fixed (byte* m = Engine.Utf8(Name)) fixed (byte* k = Engine.Utf8(key)) return Engine._api.UIModel_GetNumber(m, k); }
    }

    public string GetString(string key)
    {
        unsafe
        {
            var buffer = new byte[512];
            int length;
            fixed (byte* m = Engine.Utf8(Name)) fixed (byte* k = Engine.Utf8(key)) fixed (byte* b = buffer)
                length = Engine._api.UIModel_GetString(m, k, b, buffer.Length);
            return Encoding.UTF8.GetString(buffer, 0, System.Math.Clamp(length, 0, buffer.Length));
        }
    }

    // The oldest queued event name, or "" when none are waiting.
    public string PollEvent()
    {
        unsafe
        {
            var buffer = new byte[256];
            int length;
            fixed (byte* m = Engine.Utf8(Name)) fixed (byte* b = buffer)
                length = Engine._api.UIModel_PollEvent(m, b, buffer.Length);
            return length > 0 ? Encoding.UTF8.GetString(buffer, 0, System.Math.Min(length, buffer.Length)) : "";
        }
    }
}
