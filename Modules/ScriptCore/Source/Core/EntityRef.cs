using System;

namespace ScriptCore;

/// <summary>
/// A saveable reference to a scene entity: stores the entity's scene GUID (the 16-hex string
/// .lvl files use), so it survives save/load and script hot reloads. Resolve() finds the live
/// entity in the current world. The inspector draws it as an entity row (drag an entity in).
/// </summary>
[Serializable]
public struct EntityRef
{
    /// <summary>The entity's scene GUID (16 hex digits), or empty.</summary>
    public string Guid;

    /// <summary>Whether a GUID is set.</summary>
    public bool IsSet => !string.IsNullOrEmpty(Guid);

    /// <summary>The live entity with that GUID, or Entity.Null.</summary>
    public Entity Resolve()
    {
        if (!IsSet)
        {
            return Entity.Null;
        }
        unsafe
        {
            Entity id;
            fixed (byte* p = Engine.Utf8(Guid, stackalloc byte[Engine.Utf8StackSize]))
            {
                return Engine._api.World_FindByGuid(p, &id) != 0 ? id : Entity.Null;
            }
        }
    }

    /// <summary>The GUID, or "(none)".</summary>
    public override string ToString() => IsSet ? Guid : "(none)";
}
