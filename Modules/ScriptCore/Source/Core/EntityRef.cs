using System;

namespace ScriptCore;

// A saveable reference to a scene entity: stores the entity's scene GUID (the 16-hex string
// .lvl files use), so it survives save/load and script hot reloads. Resolve() finds the live
// entity in the current world. The inspector draws it as an entity row (drag an entity in).
[Serializable]
public struct EntityRef
{
    public string Guid;

    public bool IsSet => !string.IsNullOrEmpty(Guid);

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

    public override string ToString() => IsSet ? Guid : "(none)";
}
