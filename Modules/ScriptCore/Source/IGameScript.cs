namespace ScriptCore;

/// <summary>
/// What the engine calls on a script. Derive from Script, which implements it, rather than
/// implementing this directly.
/// </summary>
public interface IGameScript
{
    /// <summary>Once, before the first update, when the game starts (or the script's entity is spawned during play).</summary>
    void OnStart();
    /// <summary>Every frame while the game runs, after the scene's cores.</summary>
    void OnUpdate(float deltaTime);
    /// <summary>When the script's entity is destroyed, or the game stops.</summary>
    void OnDestroy();
    /// <summary>In the editor's inspector, after the script's fields (ImGui calls draw extra UI).</summary>
    void OnEditorInspect();

    /// <summary>Every fixed simulation step (before physics), while the game runs.</summary>
    void OnFixedUpdate(float fixedDeltaTime) { }

    /// <summary>
    /// Every frame after navigation and animation have moved things, before audio and rendering:
    /// cameras that follow, IK targets, props attached to animated bones.
    /// </summary>
    void OnLateUpdate(float deltaTime) { }

    /// <summary>
    /// After a hot reload: the new instance has its public fields back, but nothing else
    /// (references, statics, native state). Rebuild what OnStart set up here.
    /// </summary>
    void OnReload() { }

    /// <summary>
    /// Physics (3D and 2D), while the game runs: a contact with another collider starts.
    /// </summary>
    void OnCollisionEnter(Collision collision) { }
    /// <summary>A contact with another collider ends.</summary>
    void OnCollisionExit(Collision collision) { }
    /// <summary>
    /// Another collider enters a trigger on this entity (or on a collider child of this entity's body).
    /// </summary>
    void OnTriggerEnter(Entity other) { }
    /// <summary>Another collider leaves a trigger on this entity.</summary>
    void OnTriggerExit(Entity other) { }
}
