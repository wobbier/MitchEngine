using System;
using System.Collections.Generic;
using System.Text;

namespace ScriptCore.Dialogue;

/// <summary>
/// Shows a DialogueRunner through a UI model ("dialogue": visible, speaker, text, choosing,
/// choice0..3 / has0..3) and answers its events (dialogue_continue, dialogue_choose_0..3). The stock
/// Engine/Assets/UI/Dialogue/Dialogue.rml binds it; a project can bring its own document with the
/// same model. Call Update() every frame while it's up.
/// </summary>
public sealed class DialogueView
{
    public const int MaxChoices = 4;
    public readonly string Model;
    public DialogueRunner? Runner { get; private set; }

    public DialogueView(string model = "dialogue")
    {
        Model = model;
        var ui = UI.Model(Model);
        ui.On("dialogue_continue");
        for (int i = 0; i < MaxChoices; ++i) ui.On("dialogue_choose_" + i);
        ui.Set("visible", false);
        Refresh();
    }

    public void Show(DialogueRunner runner)
    {
        Runner = runner;
        Refresh();
    }

    /// <summary>
    /// Applies clicks from the document; true while the conversation runs.
    /// </summary>
    public bool Update()
    {
        var ui = UI.Model(Model);
        bool changed = false;
        for (string e; (e = ui.PollEvent()) != "";)
        {
            if (Runner == null) continue;
            if (e == "dialogue_continue") { Runner.Continue(); changed = true; }
            else if (e.StartsWith("dialogue_choose_", StringComparison.Ordinal) && int.TryParse(e.AsSpan(16), out int index)) { Runner.Choose(index); changed = true; }
        }
        if (changed) Refresh();
        return Runner?.IsRunning ?? false;
    }

    public void Refresh()
    {
        var ui = UI.Model(Model);
        var runner = Runner;
        bool running = runner?.IsRunning ?? false;
        ui.Set("visible", running);
        ui.Set("speaker", running ? runner!.Speaker : "");
        ui.Set("text", running ? runner!.Text : "");
        bool choosing = running && runner!.Current == DialogueStep.Choice;
        ui.Set("choosing", choosing);
        for (int i = 0; i < MaxChoices; ++i)
        {
            bool has = choosing && i < runner!.Choices.Count;
            ui.Set("has" + i, has);
            ui.Set("choice" + i, has ? runner!.Choices[i].Text : "");
        }
    }
}

/// <summary>
/// Reads a .dialogue (any text asset) through the VFS.
/// </summary>
public static class DialogueFiles
{
    private static readonly Dictionary<string, DialogueScript> Cache = new();

    public static DialogueScript? Load(string path)
    {
        if (Cache.TryGetValue(path, out var cached)) return cached;
        string? text = ReadText(path);
        if (text == null)
        {
            Debug.Warning($"Dialogue: can't read {path}");
            return null;
        }
        try
        {
            return Cache[path] = DialogueScript.Parse(text, path);
        }
        catch (FormatException ex)
        {
            Debug.Error($"Dialogue: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Forget parsed files (after editing one).
    /// </summary>
    public static void Clear() => Cache.Clear();

    private static unsafe string? ReadText(string path)
    {
        var buffer = new byte[8192];
        int length;
        fixed (byte* p = Engine.Utf8(path, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* b = buffer) length = Engine._api.Asset_ReadText(p, b, buffer.Length);
        if (length < 0) return null;
        if (length >= buffer.Length)
        {
            buffer = new byte[length + 1];
            fixed (byte* p = Engine.Utf8(path, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* b = buffer) length = Engine._api.Asset_ReadText(p, b, buffer.Length);
        }
        return Encoding.UTF8.GetString(buffer, 0, Math.Clamp(length, 0, buffer.Length - 1));
    }
}
