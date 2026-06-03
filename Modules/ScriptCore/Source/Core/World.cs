namespace ScriptCore;

public static unsafe class World
{
    public static Entity CreateEntity( string name )
    {
        Entity e = default;
        fixed (byte* p = Engine.Utf8(name)) Engine._api.World_CreateEntity(p, &e);
        return e;
    }


    public static Entity Find( string name )
    {
        Entity e = default;
        fixed (byte* p = Engine.Utf8(name)) Engine._api.World_FindByName(p, &e);
        return e;
    }


    public static Transform GetTransformByName( string name )
    {
        var e = Find(name);
        return e ? e.GetComponent<Transform>() : null;
    }
}
