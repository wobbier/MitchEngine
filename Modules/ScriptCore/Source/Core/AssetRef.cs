using System;

namespace ScriptCore;

// A saveable reference to an asset by project path (how the engine references assets, e.g.
// "Assets/Textures/Star.png"). The inspector draws it as an asset row: drag a file from the
// asset browser, pick with "...", clear with "x".
[Serializable]
public struct AssetRef
{
    public string Path;

    public bool IsSet => !string.IsNullOrEmpty(Path);

    public override string ToString() => IsSet ? Path : "(none)";

    // The data asset this references (DataAssets.Load: cached, shared, hot reloaded).
    public T? Load<T>() where T : DataAsset => DataAssets.Load<T>(Path);
}
