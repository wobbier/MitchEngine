using System;
using System.Collections.Generic;

namespace ScriptCore.AI;

// GAME-3: behavior trees authored in C#. Build a tree once, Tick it every update:
//
//   _tree = BT.Selector(
//       BT.Sequence(BT.Condition(() => _sight.CanSee), BT.Action(Chase)),
//       BT.Sequence(BT.Condition(() => _memory.HasLastSeen), BT.Action(Search)),
//       BT.Action(Patrol));
//   ...
//   _tree.Tick(new BTContext(_blackboard, deltaTime));
//
// Selector and Sequence re-evaluate from their first child every tick (higher-priority branches
// interrupt lower ones); MemorySequence resumes where it was. Action / Condition wrap lambdas.

public enum BTStatus
{
    Success,
    Failure,
    Running,
}

public readonly record struct BTContext(Blackboard Blackboard, float DeltaTime);

// Named values shared by a tree's nodes (and whoever else holds it).
public sealed class Blackboard
{
    private readonly Dictionary<string, object?> _values = new();

    public void Set<T>(string key, T value) => _values[key] = value;
    public T Get<T>(string key, T fallback = default!) => _values.TryGetValue(key, out var v) && v is T typed ? typed : fallback;
    public bool Has(string key) => _values.ContainsKey(key);
    public bool Remove(string key) => _values.Remove(key);
    public void Clear() => _values.Clear();
}

public abstract class BTNode
{
    public string Name = "";
    // The node that ran last tick under a composite (for debugging / logs).
    public BTStatus LastStatus { get; private set; } = BTStatus.Failure;

    public BTStatus Tick(in BTContext context)
    {
        LastStatus = Run(context);
        return LastStatus;
    }

    protected abstract BTStatus Run(in BTContext context);
    // Called when a running node stops being ticked (a higher branch took over).
    public virtual void Reset() { }
}

public sealed class Selector : BTNode
{
    private readonly BTNode[] _children;
    private int _running = -1;
    public Selector(params BTNode[] children) => _children = children;

    protected override BTStatus Run(in BTContext context)
    {
        for (int i = 0; i < _children.Length; ++i)
        {
            var status = _children[i].Tick(context);
            if (status == BTStatus.Failure) continue;
            if (_running >= 0 && _running != i) _children[_running].Reset();
            _running = status == BTStatus.Running ? i : -1;
            return status;
        }
        if (_running >= 0) _children[_running].Reset();
        _running = -1;
        return BTStatus.Failure;
    }

    public override void Reset()
    {
        if (_running >= 0) _children[_running].Reset();
        _running = -1;
    }
}

public sealed class Sequence : BTNode
{
    private readonly BTNode[] _children;
    private int _running = -1;
    public Sequence(params BTNode[] children) => _children = children;

    protected override BTStatus Run(in BTContext context)
    {
        for (int i = 0; i < _children.Length; ++i)
        {
            var status = _children[i].Tick(context);
            if (status == BTStatus.Success) continue;
            if (_running >= 0 && _running != i) _children[_running].Reset();
            _running = status == BTStatus.Running ? i : -1;
            return status;
        }
        _running = -1;
        return BTStatus.Success;
    }

    public override void Reset()
    {
        if (_running >= 0) _children[_running].Reset();
        _running = -1;
    }
}

// A sequence that carries on from the child that was running (steps that must finish in order).
public sealed class MemorySequence : BTNode
{
    private readonly BTNode[] _children;
    private int _current;
    public MemorySequence(params BTNode[] children) => _children = children;

    protected override BTStatus Run(in BTContext context)
    {
        while (_current < _children.Length)
        {
            var status = _children[_current].Tick(context);
            if (status == BTStatus.Running) return status;
            if (status == BTStatus.Failure) { _current = 0; return status; }
            ++_current;
        }
        _current = 0;
        return BTStatus.Success;
    }

    public override void Reset()
    {
        if (_current < _children.Length) _children[_current].Reset();
        _current = 0;
    }
}

public sealed class ActionNode : BTNode
{
    private readonly Func<BTContext, BTStatus> _run;
    private readonly Action? _reset;
    public ActionNode(Func<BTContext, BTStatus> run, Action? reset = null) { _run = run; _reset = reset; }
    protected override BTStatus Run(in BTContext context) => _run(context);
    public override void Reset() => _reset?.Invoke();
}

public sealed class ConditionNode : BTNode
{
    private readonly Func<BTContext, bool> _test;
    public ConditionNode(Func<BTContext, bool> test) => _test = test;
    protected override BTStatus Run(in BTContext context) => _test(context) ? BTStatus.Success : BTStatus.Failure;
}

public sealed class Inverter : BTNode
{
    private readonly BTNode _child;
    public Inverter(BTNode child) => _child = child;
    protected override BTStatus Run(in BTContext context) => _child.Tick(context) switch
    {
        BTStatus.Success => BTStatus.Failure,
        BTStatus.Failure => BTStatus.Success,
        _ => BTStatus.Running,
    };
    public override void Reset() => _child.Reset();
}

// Running for Seconds, then Success.
public sealed class Wait : BTNode
{
    private readonly float _seconds;
    private float _elapsed;
    public Wait(float seconds) => _seconds = seconds;

    protected override BTStatus Run(in BTContext context)
    {
        _elapsed += context.DeltaTime;
        if (_elapsed < _seconds) return BTStatus.Running;
        _elapsed = 0f;
        return BTStatus.Success;
    }

    public override void Reset() => _elapsed = 0f;
}

// Fails for Seconds after its child succeeds (don't bark every frame).
public sealed class Cooldown : BTNode
{
    private readonly BTNode _child;
    private readonly float _seconds;
    private float _left;
    public Cooldown(float seconds, BTNode child) { _seconds = seconds; _child = child; }

    protected override BTStatus Run(in BTContext context)
    {
        if (_left > 0f) { _left -= context.DeltaTime; return BTStatus.Failure; }
        var status = _child.Tick(context);
        if (status == BTStatus.Success) _left = _seconds;
        return status;
    }
}

public static class BT
{
    public static Selector Selector(params BTNode[] children) => new(children);
    public static Sequence Sequence(params BTNode[] children) => new(children);
    public static MemorySequence MemorySequence(params BTNode[] children) => new(children);
    public static ActionNode Action(Func<BTContext, BTStatus> run, Action? reset = null) => new(run, reset);
    public static ActionNode Action(Func<BTStatus> run, Action? reset = null) => new(_ => run(), reset);
    public static ConditionNode Condition(Func<bool> test) => new(_ => test());
    public static ConditionNode Condition(Func<BTContext, bool> test) => new(test);
    public static Inverter Not(BTNode child) => new(child);
    public static Wait Wait(float seconds) => new(seconds);
    public static Cooldown Cooldown(float seconds, BTNode child) => new(seconds, child);
    // Runs an action once and succeeds.
    public static ActionNode Do(Action action) => new(_ => { action(); return BTStatus.Success; });
}
