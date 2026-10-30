using System;
using System.Collections.Generic;

namespace ScriptCore.AI;

/// <summary>What a behaviour tree node reports each tick.</summary>
public enum BTStatus
{
    /// <summary>It finished and worked.</summary>
    Success,
    /// <summary>It finished and didn't work (or doesn't apply).</summary>
    Failure,
    /// <summary>It needs more ticks.</summary>
    Running,
}

/// <summary>What a tick passes down the tree.</summary>
/// <param name="Blackboard">The tree's shared values.</param>
/// <param name="DeltaTime">Seconds since the last tick.</param>
public readonly record struct BTContext(Blackboard Blackboard, float DeltaTime);

/// <summary>
/// Named values shared by a tree's nodes (and whoever else holds it).
/// </summary>
public sealed class Blackboard
{
    private readonly Dictionary<string, object?> _values = new();

    /// <summary>Stores a value under key.</summary>
    public void Set<T>(string key, T value) => _values[key] = value;
    /// <summary>The value under key as T, or the fallback when it's missing or another type.</summary>
    public T Get<T>(string key, T fallback = default!) => _values.TryGetValue(key, out var v) && v is T typed ? typed : fallback;
    /// <summary>Whether key has a value.</summary>
    public bool Has(string key) => _values.ContainsKey(key);
    /// <summary>Removes key; false when it had no value.</summary>
    public bool Remove(string key) => _values.Remove(key);
    /// <summary>Removes every value.</summary>
    public void Clear() => _values.Clear();
}

/// <summary>A behaviour tree node: Tick runs it and remembers LastStatus.</summary>
public abstract class BTNode
{
    /// <summary>A name for debugging.</summary>
    public string Name = "";
    /// <summary>
    /// The node that ran last tick under a composite (for debugging / logs).
    /// </summary>
    public BTStatus LastStatus { get; private set; } = BTStatus.Failure;

    /// <summary>Runs the node once and returns (and remembers) its status.</summary>
    public BTStatus Tick(in BTContext context)
    {
        LastStatus = Run(context);
        return LastStatus;
    }

    /// <summary>What the node does on a tick.</summary>
    protected abstract BTStatus Run(in BTContext context);
    /// <summary>
    /// Called when a running node stops being ticked (a higher branch took over).
    /// </summary>
    public virtual void Reset() { }
}

/// <summary>Ticks its children in order until one doesn't fail: higher-priority branches come first and interrupt lower ones.</summary>
public sealed class Selector : BTNode
{
    private readonly BTNode[] _children;
    private int _running = -1;
    /// <summary>A selector over children, highest priority first.</summary>
    public Selector(params BTNode[] children) => _children = children;

    /// <inheritdoc/>
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

    /// <inheritdoc/>
    public override void Reset()
    {
        if (_running >= 0) _children[_running].Reset();
        _running = -1;
    }
}

/// <summary>Ticks its children in order while they succeed, from the first child every tick.</summary>
public sealed class Sequence : BTNode
{
    private readonly BTNode[] _children;
    private int _running = -1;
    /// <summary>A sequence of children.</summary>
    public Sequence(params BTNode[] children) => _children = children;

    /// <inheritdoc/>
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

    /// <inheritdoc/>
    public override void Reset()
    {
        if (_running >= 0) _children[_running].Reset();
        _running = -1;
    }
}

/// <summary>
/// A sequence that carries on from the child that was running (steps that must finish in order).
/// </summary>
public sealed class MemorySequence : BTNode
{
    private readonly BTNode[] _children;
    private int _current;
    /// <summary>A memory sequence of children.</summary>
    public MemorySequence(params BTNode[] children) => _children = children;

    /// <inheritdoc/>
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

    /// <inheritdoc/>
    public override void Reset()
    {
        if (_current < _children.Length) _children[_current].Reset();
        _current = 0;
    }
}

/// <summary>A leaf that runs a function (BT.Action).</summary>
public sealed class ActionNode : BTNode
{
    private readonly Func<BTContext, BTStatus> _run;
    private readonly Action? _reset;
    /// <summary>A leaf from a function, and an optional reset when it's interrupted.</summary>
    public ActionNode(Func<BTContext, BTStatus> run, Action? reset = null) { _run = run; _reset = reset; }
    /// <inheritdoc/>
    protected override BTStatus Run(in BTContext context) => _run(context);
    /// <inheritdoc/>
    public override void Reset() => _reset?.Invoke();
}

/// <summary>A leaf that succeeds when a test holds (BT.Condition).</summary>
public sealed class ConditionNode : BTNode
{
    private readonly Func<BTContext, bool> _test;
    /// <summary>A condition from a test.</summary>
    public ConditionNode(Func<BTContext, bool> test) => _test = test;
    /// <inheritdoc/>
    protected override BTStatus Run(in BTContext context) => _test(context) ? BTStatus.Success : BTStatus.Failure;
}

/// <summary>Swaps its child's Success and Failure (BT.Not).</summary>
public sealed class Inverter : BTNode
{
    private readonly BTNode _child;
    /// <summary>Inverts child.</summary>
    public Inverter(BTNode child) => _child = child;
    /// <inheritdoc/>
    protected override BTStatus Run(in BTContext context) => _child.Tick(context) switch
    {
        BTStatus.Success => BTStatus.Failure,
        BTStatus.Failure => BTStatus.Success,
        _ => BTStatus.Running,
    };
    /// <inheritdoc/>
    public override void Reset() => _child.Reset();
}

/// <summary>
/// Running for Seconds, then Success.
/// </summary>
public sealed class Wait : BTNode
{
    private readonly float _seconds;
    private float _elapsed;
    /// <summary>Waits that many seconds.</summary>
    public Wait(float seconds) => _seconds = seconds;

    /// <inheritdoc/>
    protected override BTStatus Run(in BTContext context)
    {
        _elapsed += context.DeltaTime;
        if (_elapsed < _seconds) return BTStatus.Running;
        _elapsed = 0f;
        return BTStatus.Success;
    }

    /// <inheritdoc/>
    public override void Reset() => _elapsed = 0f;
}

/// <summary>
/// Fails for Seconds after its child succeeds (don't bark every frame).
/// </summary>
public sealed class Cooldown : BTNode
{
    private readonly BTNode _child;
    private readonly float _seconds;
    private float _left;
    /// <summary>Ticks child, then fails for seconds after it succeeds.</summary>
    public Cooldown(float seconds, BTNode child) { _seconds = seconds; _child = child; }

    /// <inheritdoc/>
    protected override BTStatus Run(in BTContext context)
    {
        if (_left > 0f) { _left -= context.DeltaTime; return BTStatus.Failure; }
        var status = _child.Tick(context);
        if (status == BTStatus.Success) _left = _seconds;
        return status;
    }
}

/// <summary>
/// GAME-3: behaviour trees authored in C#. Build a tree once, Tick it every update:
/// <code>
///   _tree = BT.Selector(
///       BT.Sequence(BT.Condition(() =&gt; _sight.CanSee), BT.Action(Chase)),
///       BT.Sequence(BT.Condition(() =&gt; _memory.HasLastSeen), BT.Action(Search)),
///       BT.Action(Patrol));
///   ...
///   _tree.Tick(new BTContext(_blackboard, deltaTime));
/// </code>
/// Selector and Sequence re-evaluate from their first child every tick (higher-priority branches
/// interrupt lower ones); MemorySequence resumes where it was. Action / Condition wrap lambdas.
/// </summary>
public static class BT
{
    /// <summary>A Selector over children.</summary>
    public static Selector Selector(params BTNode[] children) => new(children);
    /// <summary>A Sequence of children.</summary>
    public static Sequence Sequence(params BTNode[] children) => new(children);
    /// <summary>A MemorySequence of children.</summary>
    public static MemorySequence MemorySequence(params BTNode[] children) => new(children);
    /// <summary>An action leaf.</summary>
    public static ActionNode Action(Func<BTContext, BTStatus> run, Action? reset = null) => new(run, reset);
    /// <summary>An action leaf that ignores the context.</summary>
    public static ActionNode Action(Func<BTStatus> run, Action? reset = null) => new(_ => run(), reset);
    /// <summary>A condition leaf.</summary>
    public static ConditionNode Condition(Func<bool> test) => new(_ => test());
    /// <summary>A condition leaf that reads the context.</summary>
    public static ConditionNode Condition(Func<BTContext, bool> test) => new(test);
    /// <summary>Inverts a node.</summary>
    public static Inverter Not(BTNode child) => new(child);
    /// <summary>Running for seconds, then Success.</summary>
    public static Wait Wait(float seconds) => new(seconds);
    /// <summary>Fails for seconds after child succeeds.</summary>
    public static Cooldown Cooldown(float seconds, BTNode child) => new(seconds, child);
    /// <summary>
    /// Runs an action once and succeeds.
    /// </summary>
    public static ActionNode Do(Action action) => new(_ => { action(); return BTStatus.Success; });
}
