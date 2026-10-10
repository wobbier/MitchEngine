namespace ScriptCore;

public interface IGameScript
{
    void OnStart();
    void OnUpdate(float deltaTime);
    void OnDestroy();
    void OnEditorInspect();

    // Every fixed simulation step (before physics), while the game runs.
    void OnFixedUpdate(float fixedDeltaTime) { }

    // After a hot reload: the new instance has its public fields back, but nothing else
    // (references, statics, native state). Rebuild what OnStart set up here.
    void OnReload() { }
}
