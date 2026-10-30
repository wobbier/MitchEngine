using System;
using System.Collections.Generic;
using ScriptCore.AI;

namespace ScriptCore.Kit;

/// <summary>
/// GAME-3's sample AI, on an entity with a NavMeshAgent: patrols Waypoints (entity names, comma
/// separated), chases Target when it sees it, stops when it catches it, and when it loses sight
/// searches where it last saw (or heard) it before going back to patrolling. State shows what it's
/// doing (Patrol, Chase, Caught, Search); changes are logged ("Guard: sees Intruder").
/// </summary>
public class GuardBrain : Script
{
    /// <summary>The entity it hunts (by name).</summary>
    public string Target = "Player";
    /// <summary>Entity names to patrol between, comma-separated.</summary>
    public string Waypoints = "";
    /// <summary>Walking speed on patrol, metres per second.</summary>
    public float PatrolSpeed = 2.5f;
    /// <summary>Running speed when chasing.</summary>
    public float ChaseSpeed = 5.0f;
    /// <summary>How close counts as caught, metres.</summary>
    public float CatchDistance = 1.5f;
    /// <summary>How far it sees, metres.</summary>
    public float SightRange = 12f;
    /// <summary>Its view cone, degrees.</summary>
    public float FieldOfView = 120f;
    /// <summary>Seconds spent at each waypoint and at a searched spot.</summary>
    [Tooltip("Seconds spent at each waypoint and at a searched spot")]
    public float PauseSeconds = 1.0f;
    /// <summary>Patrol, Chase or Search (readable for UI and tests).</summary>
    public string State = "Patrol";

    /// <summary>What it sees and hears.</summary>
    public Perception Perception { get; } = new();
    /// <summary>The behaviour tree's shared values.</summary>
    public Blackboard Blackboard { get; } = new();

    private NavMeshAgent? _agent;
    private Entity _target;
    private readonly List<Vector3> _route = new();
    private int _waypoint;
    private BTNode? _tree;

    /// <inheritdoc/>
    public override void OnStart()
    {
        _agent = HasComponent<NavMeshAgent>() ? GetComponent<NavMeshAgent>() : null;
        _target = Target.Length > 0 ? World.Find(Target) : default;
        _route.Clear();
        foreach (var name in Waypoints.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var point = World.Find(name);
            if (point) _route.Add(point.Transform.WorldPosition);
            else Debug.Warning($"{Entity.Name}: no waypoint named '{name}'");
        }
        Perception.SightRange = SightRange;
        Perception.FieldOfView = FieldOfView;
        _tree = BT.Selector(
            BT.Sequence(BT.Condition(() => Perception.CanSee), BT.Action(Chase)),
            BT.Sequence(BT.Condition(() => Perception.HasLastKnown), BT.MemorySequence(BT.Action(GoToLastKnown, StopMoving), BT.Wait(PauseSeconds), BT.Do(GiveUp))),
            BT.MemorySequence(BT.Action(Patrol, StopMoving), BT.Wait(PauseSeconds), BT.Do(NextWaypoint)));
    }

    /// <inheritdoc/>
    public override void OnReload() => OnStart();

    /// <inheritdoc/>
    public override void OnUpdate(float deltaTime)
    {
        if (_agent == null || _tree == null) return;
        Perception.Update(Entity, _target, deltaTime);
        _tree.Tick(new BTContext(Blackboard, deltaTime));
    }

    private void SetState(string state, string detail)
    {
        if (State == state) return;
        State = state;
        Debug.Log($"{Entity.Name}: {detail}");
    }

    private BTStatus Chase()
    {
        var at = _target.Transform.WorldPosition;
        if (Vector3.Distance(transform.WorldPosition, at) <= CatchDistance)
        {
            SetState("Caught", $"caught {_target.Name}");
            _agent!.ResetPath();
            return BTStatus.Running;
        }
        SetState("Chase", $"sees {_target.Name}");
        _agent!.Speed = ChaseSpeed;
        _agent.SetDestination(OnMesh(at));
        return BTStatus.Running;
    }

    private BTStatus GoToLastKnown()
    {
        SetState("Search", $"lost {_target.Name}, searching");
        _agent!.Speed = PatrolSpeed;
        return MoveTo(Perception.LastKnownPosition);
    }

    private void GiveUp()
    {
        Perception.Forget();
        SetState("Patrol", "gave up, back to patrol");
    }

    private BTStatus Patrol()
    {
        if (_route.Count == 0) return BTStatus.Running;
        SetState("Patrol", "patrolling");
        _agent!.Speed = PatrolSpeed;
        return MoveTo(_route[_waypoint % _route.Count]);
    }

    private void NextWaypoint()
    {
        if (_route.Count == 0) return;
        _waypoint = (_waypoint + 1) % _route.Count;
        Debug.Log($"{Entity.Name}: patrol -> waypoint {_waypoint}");
    }

    private Vector3 _moving = new Vector3(float.NaN, 0f, 0f);

    private BTStatus MoveTo(Vector3 destination)
    {
        destination = OnMesh(destination);
        if (Vector3.Distance(destination, _moving) > 0.25f || float.IsNaN(_moving.x))
        {
            _agent!.SetDestination(destination);
            _moving = destination;
            return BTStatus.Running;
        }
        var flat = transform.WorldPosition - destination;
        flat = new Vector3(flat.x, 0f, flat.z);
        return _agent!.HasArrived || flat.Length() < 0.6f ? BTStatus.Success : BTStatus.Running;
    }

    private void StopMoving() => _moving = new Vector3(float.NaN, 0f, 0f);

    private static Vector3 OnMesh(Vector3 point) => Navigation.SamplePosition(point, 3.0f, out var onMesh) ? onMesh : point;
}
