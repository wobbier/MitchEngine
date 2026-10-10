namespace ScriptCore;

// Base for scripts that live on an entity
public abstract class Script : IGameScript
{
    public Entity Entity { get; internal set; } = Entity.Null;

    public virtual void OnStart() { }
    public virtual void OnUpdate( float deltaTime ) { }
    public virtual void OnFixedUpdate( float fixedDeltaTime ) { }
    public virtual void OnDestroy() { }
    public virtual void OnEditorInspect() { }
    public virtual void OnReload() { }
    public virtual void OnCollisionEnter( Collision collision ) { }
    public virtual void OnCollisionExit( Collision collision ) { }
    public virtual void OnTriggerEnter( Entity other ) { }
    public virtual void OnTriggerExit( Entity other ) { }

    public Transform transform => Entity.GetComponent<Transform>();
    public T GetComponent<T>() where T : Component, new() => Entity.GetComponent<T>();
    public bool HasComponent<T>() where T : Component, new() => Entity.HasComponent<T>();
    public T AddComponent<T>() where T : Component, new() => Entity.AddComponent<T>();
}
