using System;

namespace ScriptCore;

// Base for scripts that live on an entity
public abstract class Script : IGameScript
{
    public Entity Entity { get; internal set; } = Entity.Null;

    // Coroutines, timers and tweens owned by this script: they stop when it's destroyed (Async.cs).
    protected AsyncHandle StartCoroutine(System.Collections.IEnumerator routine) => Scheduler.AddCoroutine(this, routine);
    protected void StopAllCoroutines() => Scheduler.RemoveOwner(this);
    protected AsyncHandle After(float seconds, Action action) => Timer.After(seconds, action, this);
    protected AsyncHandle Every(float seconds, Action action) => Timer.Every(seconds, action, this);

    // Subscribes to an event for as long as this script lives (EventBus).
    protected IDisposable On<T>(Action<T> handler) => EventBus.Subscribe(handler);

    public virtual void OnStart() { }
    public virtual void OnUpdate( float deltaTime ) { }
    public virtual void OnFixedUpdate( float fixedDeltaTime ) { }
    public virtual void OnLateUpdate( float deltaTime ) { }
    public virtual void OnDestroy() { }
    public virtual void OnEditorInspect() { }
    public virtual void OnReload() { }
    // ME-291: a Net.PredictedEvent this script raised, presented once per (tick, event) on each
    // peer: right away on the server and the predicting owner, from the server on other clients.
    public virtual void OnNetEvent( int eventId, int value ) { }
    public virtual void OnCollisionEnter( Collision collision ) { }
    public virtual void OnCollisionExit( Collision collision ) { }
    public virtual void OnTriggerEnter( Entity other ) { }
    public virtual void OnTriggerExit( Entity other ) { }

    public Transform transform => Entity.GetComponent<Transform>();
    public T GetComponent<T>() where T : Component, new() => Entity.GetComponent<T>();
    public bool HasComponent<T>() where T : Component, new() => Entity.HasComponent<T>();
    public T AddComponent<T>() where T : Component, new() => Entity.AddComponent<T>();
}
