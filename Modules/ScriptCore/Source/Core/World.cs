namespace ScriptCore;

public static unsafe class World
{
    public static Entity CreateEntity( string name )
    {
        Entity e = default;
        fixed (byte* p = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize])) Engine._api.World_CreateEntity(p, &e);
        return e;
    }


    public static Entity Find( string name )
    {
        Entity e = default;
        fixed (byte* p = Engine.Utf8(name, stackalloc byte[Engine.Utf8StackSize])) Engine._api.World_FindByName(p, &e);
        return e;
    }


    // Spawns a prefab (optionally under a parent) and returns its root entity.
    public static Entity Instantiate( string prefab, Entity parent = default )
    {
        Entity e = default;
        fixed (byte* p = Engine.Utf8(prefab, stackalloc byte[Engine.Utf8StackSize])) Engine._api.World_Instantiate(p, parent, &e);
        return e;
    }


    // Loads a scene at the end of this frame.
    public static void LoadScene( string scene )
    {
        fixed (byte* p = Engine.Utf8(scene, stackalloc byte[Engine.Utf8StackSize])) Engine._api.World_LoadScene(p);
    }


    // Loads a scene in the background under a loading screen (PLAY-5): the current scene keeps
    // running while the file is read, and the screen stays up until the new scene's assets have
    // loaded. loadingScreen: "" = the project's (Assets/UI/Loading/Loading.rml by default), "none"
    // = no screen. False when the scene doesn't exist or a load is already running.
    public static bool LoadSceneAsync( string scene, string loadingScreen = "" )
    {
        fixed (byte* p = Engine.Utf8(scene, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* d = Engine.Utf8(loadingScreen ?? "", stackalloc byte[Engine.Utf8StackSize])) return Engine._api.World_LoadSceneAsync(p, d) != 0;
    }

    // 0..1 while LoadSceneAsync runs; 1 otherwise.
    public static float LoadProgress => Engine._api.World_GetLoadProgress();
    public static bool IsLoadingScene => Engine._api.World_IsLoadingScene() != 0;


    // Loads a scene on top of this one (a streamed level chunk, a shared lighting scene) and returns
    // its id. Async (the default) reads it in the background and adds it at the start of a later
    // frame: watch SceneState. Its entities unload together and go when the main scene changes.
    public static int LoadSceneAdditive( string scene, bool async = true )
    {
        fixed (byte* p = Engine.Utf8(scene, stackalloc byte[Engine.Utf8StackSize])) return Engine._api.World_LoadSceneAdditive(p, async ? (byte)1 : (byte)0);
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
