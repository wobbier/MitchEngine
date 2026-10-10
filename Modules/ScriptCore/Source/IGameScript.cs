namespace ScriptCore;

public interface IGameScript
{
    void OnStart();
    void OnUpdate(float deltaTime);
    void OnDestroy();
    void OnEditorInspect();

    // Every fixed simulation step (before physics), while the game runs.
    void OnFixedUpdate(float fixedDeltaTime) { }

    // Every frame after navigation and animation have moved things, before audio and rendering:
    // cameras that follow, IK targets, props attached to animated bones.
    void OnLateUpdate(float deltaTime) { }

    // After a hot reload: the new instance has its public fields back, but nothing else
    // (references, statics, native state). Rebuild what OnStart set up here.
    void OnReload() { }

    // Physics (3D and 2D), while the game runs: contacts start / stop, or another collider enters /
    // leaves a trigger on this entity (or on a collider child of this entity's body).
    void OnCollisionEnter(Collision collision) { }
    void OnCollisionExit(Collision collision) { }
    void OnTriggerEnter(Entity other) { }
    void OnTriggerExit(Entity other) { }
}
