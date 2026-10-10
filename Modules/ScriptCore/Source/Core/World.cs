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


    // Loads a scene on top of this one (a streamed level chunk, a shared lighting scene) and returns
    // its id. Async (the default) reads it in the background and adds it at the start of a later
    // frame: watch SceneState. Its entities unload together and go when the main scene changes.
    public static int LoadSceneAdditive( string scene, bool async = true )
    {
        fixed (byte* p = Engine.Utf8(scene)) return Engine._api.World_LoadSceneAdditive(p, async ? (byte)1 : (byte)0);
    }


    public static bool UnloadScene( int scene ) => Engine._api.World_UnloadScene(scene) != 0;


    public static SceneState GetSceneState( int scene ) => (SceneState)Engine._api.World_GetSceneState(scene);


    public static Transform GetTransformByName( string name )
    {
        var e = Find(name);
        return e ? e.GetComponent<Transform>() : null;
    }
}

// An additive scene's progress (World.GetSceneState).
public enum SceneState
{
    None = 0,   // unknown id, or unloaded
    Loading,
    Loaded,
    Failed,
}
