namespace ScriptCore;

/// <summary>
/// Walks the navmesh (Engine/Docs/Navigation.md). Paths are computed by the engine's navigation
/// update, so HasPath turns true (and the agent starts moving) a frame after SetDestination.
/// </summary>
public class NavMeshAgent : Component
{
    /// <summary>Asks for a path to destination (it starts moving a frame later); false when it isn't on a navmesh.</summary>
    public bool SetDestination(Vector3 destination)
    {
        unsafe { return Engine._api.NavAgent_SetDestination(Entity, &destination) != 0; }
    }

    /// <summary>
    /// Stopped agents keep their path and continue where they were when resumed.
    /// </summary>
    public bool IsStopped
    {
        set { unsafe { Engine._api.NavAgent_SetStopped(Entity, value ? (byte)1 : (byte)0); } }
    }

    /// <summary>Drops the path and stops.</summary>
    public void ResetPath()
    {
        unsafe { Engine._api.NavAgent_ResetPath(Entity); }
    }

    /// <summary>
    /// Moves the agent instantly and drops its path.
    /// </summary>
    public void Warp(Vector3 position)
    {
        unsafe { Engine._api.NavAgent_Warp(Entity, &position); }
    }

    /// <summary>
    /// True from SetDestination until it arrives (including while the path is being computed).
    /// </summary>
    public bool HasPath { get { unsafe { return Engine._api.NavAgent_HasPath(Entity) != 0; } } }
    /// <summary>Whether it reached its destination.</summary>
    public bool HasArrived { get { unsafe { return Engine._api.NavAgent_HasArrived(Entity) != 0; } } }
    /// <summary>How far along the path to the destination, in metres.</summary>
    public float RemainingDistance { get { unsafe { return Engine._api.NavAgent_GetRemainingDistance(Entity); } } }

    /// <summary>The agent's velocity this frame.</summary>
    public Vector3 Velocity
    {
        get { unsafe { Vector3 v; Engine._api.NavAgent_GetVelocity(Entity, &v); return v; } }
    }

    /// <summary>
    /// Where the crowd wants to go this frame (move a character controller with it when the agent
    /// doesn't update its own position).
    /// </summary>
    public Vector3 DesiredVelocity
    {
        get { unsafe { Vector3 v; Engine._api.NavAgent_GetDesiredVelocity(Entity, &v); return v; } }
    }

    /// <summary>The agent's top speed, in metres per second.</summary>
    public float Speed
    {
        get { unsafe { return Engine._api.NavAgent_GetSpeed(Entity); } }
        set { unsafe { Engine._api.NavAgent_SetSpeed(Entity, value); } }
    }
}
