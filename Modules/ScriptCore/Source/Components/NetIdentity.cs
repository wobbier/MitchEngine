namespace ScriptCore;

/// <summary>
/// Marks an entity as networked (NET-3; ME-312 wrapper): the server sends its pose and its scripts'
/// [Replicated] fields to every client. Placed in a scene it has a static NetId, the same on every
/// peer; added or instantiated while a session runs (on the server) it gets a dynamic NetId and every
/// client creates its copy from Prefab (empty: the prefab the entity was instantiated from, else a
/// bare entity). Add it before the entity's first update: entity.AddComponent&lt;NetIdentity&gt;().
/// </summary>
public class NetIdentity : Component
{
    /// <summary>
    /// The prefab clients create for a copy spawned during a session ("Assets/Prefabs/Pawn.prefab");
    /// empty takes the asset World.Instantiate made the entity from.
    /// </summary>
    public string Prefab
    {
        get => Entity.GetField<string>("NetIdentity", "Prefab", "");
        set => Entity.SetField("NetIdentity", "Prefab", value ?? "");
    }

    /// <summary>
    /// Relevancy (NET-13): how much its updates matter next to others' when a client's bandwidth runs
    /// short (default 1).
    /// </summary>
    public float Priority
    {
        get => Entity.GetField<float>("NetIdentity", "Priority", 1f);
        set => Entity.SetField("NetIdentity", "Priority", value);
    }

    /// <summary>Sent to every client wherever it is, ignoring the server's relevant radius (a game mode, a scoreboard).</summary>
    public bool AlwaysRelevant
    {
        get => Entity.GetField<bool>("NetIdentity", "AlwaysRelevant", false);
        set => Entity.SetField("NetIdentity", "AlwaysRelevant", value);
    }

    /// <summary>Its id on the wire, the same on every peer (0 outside a session, or before it's assigned).</summary>
    public int NetId => Net.GetNetId(Entity);

    /// <summary>
    /// The client that owns (and predicts) it, 0 for the server. Setting it is the server's: the new
    /// owner predicts it from then on (Net.SetOwner).
    /// </summary>
    public int Owner
    {
        get => Net.GetOwner(Entity);
        set => Net.SetOwner(Entity, value);
    }

    /// <summary>Whether this client owns (and predicts) it (Net.IsOwner).</summary>
    public bool IsLocalOwner => Net.IsOwner(Entity);
}
