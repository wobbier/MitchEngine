using System;

namespace ScriptCore;

/// <summary>
/// Base for scripts that live on an entity
/// </summary>
public abstract class Script : IGameScript
{
    /// <summary>The entity this script is on.</summary>
    public Entity Entity { get; internal set; } = Entity.Null;

    /// <summary>
    /// Coroutines, timers and tweens owned by this script: they stop when it's destroyed (Async.cs).
    /// </summary>
    protected AsyncHandle StartCoroutine(System.Collections.IEnumerator routine) => Scheduler.AddCoroutine(this, routine);
    /// <summary>Stops every coroutine, timer and tween this script started.</summary>
    protected void StopAllCoroutines() => Scheduler.RemoveOwner(this);
    /// <summary>Runs action once after that many seconds of game time (cancelled if the script goes first).</summary>
    protected AsyncHandle After(float seconds, Action action) => Timer.After(seconds, action, this);
    /// <summary>Runs action every that many seconds of game time while the script lives.</summary>
    protected AsyncHandle Every(float seconds, Action action) => Timer.Every(seconds, action, this);

    /// <summary>
    /// Subscribes to an event for as long as this script lives (EventBus).
    /// </summary>
    protected IDisposable On<T>(Action<T> handler) => EventBus.Subscribe(handler);

    /// <summary>Once, before the first update, when the game starts or the entity is spawned during play.</summary>
    public virtual void OnStart() { }
    /// <summary>Every frame while the game runs, after the scene's cores.</summary>
    public virtual void OnUpdate( float deltaTime ) { }
    /// <summary>Every fixed simulation step, before physics.</summary>
    public virtual void OnFixedUpdate( float fixedDeltaTime ) { }
    /// <summary>Every frame after animation and navigation, before audio and rendering (follow cameras, IK targets).</summary>
    public virtual void OnLateUpdate( float deltaTime ) { }
    /// <summary>When the entity is destroyed or the game stops.</summary>
    public virtual void OnDestroy() { }
    /// <summary>In the editor's inspector, below the script's fields: extra ImGui UI.</summary>
    public virtual void OnEditorInspect() { }
    /// <summary>After a hot reload: public fields are back, nothing else is; rebuild what OnStart set up.</summary>
    public virtual void OnReload() { }
    /// <summary>
    /// ME-291: a Net.PredictedEvent this script raised, presented once per (tick, event) on each
    /// peer: right away on the server and the predicting owner, from the server on other clients.
    /// </summary>
    public virtual void OnNetEvent( int eventId, int value ) { }
    /// <summary>A contact with another collider starts.</summary>
    public virtual void OnCollisionEnter( Collision collision ) { }
    /// <summary>A contact with another collider ends.</summary>
    public virtual void OnCollisionExit( Collision collision ) { }
    /// <summary>Another collider enters a trigger on this entity.</summary>
    public virtual void OnTriggerEnter( Entity other ) { }
    /// <summary>Another collider leaves a trigger on this entity.</summary>
    public virtual void OnTriggerExit( Entity other ) { }

    /// <summary>This entity's Transform.</summary>
    public Transform transform => Entity.GetComponent<Transform>();
    /// <summary>This entity's component of type T, or null.</summary>
    public T GetComponent<T>() where T : Component, new() => Entity.GetComponent<T>();
    /// <summary>Whether this entity has a component of type T.</summary>
    public bool HasComponent<T>() where T : Component, new() => Entity.HasComponent<T>();
    /// <summary>Adds a component of type T to this entity and returns it.</summary>
    public T AddComponent<T>() where T : Component, new() => Entity.AddComponent<T>();
}
