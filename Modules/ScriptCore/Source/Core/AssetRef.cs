using System;

namespace ScriptCore;

/// <summary>
/// A saveable reference to an asset by project path (how the engine references assets, e.g.
/// "Assets/Textures/Star.png"). The inspector draws it as an asset row: drag a file from the
/// asset browser, pick with "...", clear with "x".
/// </summary>
[Serializable]
public struct AssetRef
{
    /// <summary>The project path ("Assets/..."), or empty.</summary>
    public string Path;

    /// <summary>Whether a path is set.</summary>
    public bool IsSet => !string.IsNullOrEmpty(Path);

    /// <summary>The path, or "(none)".</summary>
    public override string ToString() => IsSet ? Path : "(none)";

    /// <summary>
    /// The data asset this references (DataAssets.Load: cached, shared, hot reloaded).
    /// </summary>
    public T? Load<T>() where T : DataAsset => DataAssets.Load<T>(Path);
}
