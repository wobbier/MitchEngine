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


    // Spawns a prefab (optionally under a parent) and returns its root entity.
    public static Entity Instantiate( string prefab, Entity parent = default )
    {
        Entity e = default;
        fixed (byte* p = Engine.Utf8(prefab)) Engine._api.World_Instantiate(p, parent, &e);
        return e;
    }


    // Loads a scene at the end of this frame.
    public static void LoadScene( string scene )
    {
        fixed (byte* p = Engine.Utf8(scene)) Engine._api.World_LoadScene(p);
    }


    public static Transform GetTransformByName( string name )
    {
        var e = Find(name);
        return e ? e.GetComponent<Transform>() : null;
    }
}
