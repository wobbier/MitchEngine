namespace ScriptCore;

// Walks the navmesh (Engine/Docs/Navigation.md). Paths are computed by the engine's navigation
// update, so HasPath turns true (and the agent starts moving) a frame after SetDestination.
public class NavMeshAgent : Component
{
    public bool SetDestination(Vector3 destination)
    {
        unsafe { return Engine._api.NavAgent_SetDestination(Entity, &destination) != 0; }
    }

    // Stopped agents keep their path and continue where they were when resumed.
    public bool IsStopped
    {
        set { unsafe { Engine._api.NavAgent_SetStopped(Entity, value ? (byte)1 : (byte)0); } }
    }

    public void ResetPath()
    {
        unsafe { Engine._api.NavAgent_ResetPath(Entity); }
    }

    // Moves the agent instantly and drops its path.
    public void Warp(Vector3 position)
    {
        unsafe { Engine._api.NavAgent_Warp(Entity, &position); }
    }

    // True from SetDestination until it arrives (including while the path is being computed).
    public bool HasPath { get { unsafe { return Engine._api.NavAgent_HasPath(Entity) != 0; } } }
    public bool HasArrived { get { unsafe { return Engine._api.NavAgent_HasArrived(Entity) != 0; } } }
    public float RemainingDistance { get { unsafe { return Engine._api.NavAgent_GetRemainingDistance(Entity); } } }

    public Vector3 Velocity
    {
        get { unsafe { Vector3 v; Engine._api.NavAgent_GetVelocity(Entity, &v); return v; } }
    }

    // Where the crowd wants to go this frame (move a character controller with it when the agent
    // doesn't update its own position).
    public Vector3 DesiredVelocity
    {
        get { unsafe { Vector3 v; Engine._api.NavAgent_GetDesiredVelocity(Entity, &v); return v; } }
    }

    public float Speed
    {
        get { unsafe { return Engine._api.NavAgent_GetSpeed(Entity); } }
        set { unsafe { Engine._api.NavAgent_SetSpeed(Entity, value); } }
    }
}
