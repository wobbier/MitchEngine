using System;
using System.Collections;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace ScriptCore;

// Async work on the game's clock (CS-6): await, coroutines, timers and tweens, all advanced once a
// frame on the main thread before the scripts' OnUpdate, by game time (paused = stopped).
//
//   await Async.Seconds(0.5f);                 // resumes on the main thread, 0.5 s of game time on
//   await Async.Frames(3);  await Async.NextFrame();  await Async.Until(() => door.Open);
//   StartCoroutine(Blink());                    // IEnumerator: yield return new WaitForSeconds(1f);
//   After(2f, Explode);  Every(1f, Tick);       // timers (Script helpers; cancel with the handle)
//   Tween.Float(0f, 1f, 0.3f, v => alpha = v, Ease.OutCubic);
//
// What a script starts (coroutines, timers, tweens it owns) stops when the script is destroyed;
// everything stops on Stop and on a hot reload. Plain `await Task.Delay` also resumes on the main
// thread (the main-thread SynchronizationContext), but on the wall clock.
public static class Async
{
    /// <summary>
    /// Frames ticked since play started.
    /// </summary>
    public static long Frame => Scheduler.Frame;

    public static Task Seconds(float seconds, Script? owner = null) => Scheduler.Wait(owner, seconds, 0, null);
    public static Task Frames(int frames, Script? owner = null) => Scheduler.Wait(owner, 0f, Math.Max(frames, 1), null);
    public static Task NextFrame(Script? owner = null) => Scheduler.Wait(owner, 0f, 1, null);
    public static Task Until(Func<bool> condition, Script? owner = null) => Scheduler.Wait(owner, 0f, 0, condition);
}

/// <summary>
/// The yield instructions coroutines understand (plus null = the next frame, and a nested IEnumerator).
/// </summary>
public sealed class WaitForSeconds { public readonly float Seconds; public WaitForSeconds(float seconds) { Seconds = seconds; } }
public sealed class WaitFrames { public readonly int Count; public WaitFrames(int count) { Count = Math.Max(count, 1); } }
public sealed class WaitUntil { public readonly Func<bool> Condition; public WaitUntil(Func<bool> condition) { Condition = condition; } }

/// <summary>
/// A running coroutine, timer or tween: Cancel stops it; Task completes when it ends (cancelled too).
/// </summary>
public sealed class AsyncHandle
{
    internal bool Cancelled;
    internal readonly TaskCompletionSource<bool> Done = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public bool IsRunning => !Done.Task.IsCompleted;
    public Task Task => Done.Task;
    public void Cancel() { Cancelled = true; }
}

/// <summary>
/// Curves for tweens. Linear, SmoothStep and Hold match SequenceInterpolation's.
/// </summary>
public enum Ease { Linear, SmoothStep, Hold, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic, OutBack }

public static class Easing
{
    public static float Evaluate(Ease ease, float t)
    {
        t = Math.Clamp(t, 0f, 1f);
        switch (ease)
        {
            case Ease.SmoothStep: return t * t * (3f - 2f * t);
            case Ease.Hold: return t < 1f ? 0f : 1f;
            case Ease.InQuad: return t * t;
            case Ease.OutQuad: return 1f - (1f - t) * (1f - t);
            case Ease.InOutQuad: return t < 0.5f ? 2f * t * t : 1f - MathF.Pow(-2f * t + 2f, 2f) / 2f;
            case Ease.InCubic: return t * t * t;
            case Ease.OutCubic: return 1f - MathF.Pow(1f - t, 3f);
            case Ease.InOutCubic: return t < 0.5f ? 4f * t * t * t : 1f - MathF.Pow(-2f * t + 2f, 3f) / 2f;
            case Ease.OutBack:
            {
                const float c1 = 1.70158f, c3 = c1 + 1f;
                return 1f + c3 * MathF.Pow(t - 1f, 3f) + c1 * MathF.Pow(t - 1f, 2f);
            }
            default: return t;
        }
    }
}

public static class Tween
{
    /// <summary>
    /// Calls setter with from..to over seconds of game time (eased); the last call is exactly `to`.
    /// </summary>
    public static AsyncHandle Float(float from, float to, float seconds, Action<float> setter, Ease ease = Ease.SmoothStep, Script? owner = null)
        => Scheduler.AddTween(owner, seconds, ease, t => setter(from + (to - from) * t));

    public static AsyncHandle Vector(Vector3 from, Vector3 to, float seconds, Action<Vector3> setter, Ease ease = Ease.SmoothStep, Script? owner = null)
        => Scheduler.AddTween(owner, seconds, ease, t => setter(from + (to - from) * t));

    /// <summary>
    /// Moves a transform's position to `to`.
    /// </summary>
    public static AsyncHandle Move(Transform transform, Vector3 to, float seconds, Ease ease = Ease.SmoothStep, Script? owner = null)
    {
        Vector3 from = transform.Position;
        return Vector(from, to, seconds, p => transform.Position = p, ease, owner);
    }
}

public static class Timer
{
    public static AsyncHandle After(float seconds, Action action, Script? owner = null) => Scheduler.AddTimer(owner, seconds, false, action);
    public static AsyncHandle Every(float seconds, Action action, Script? owner = null) => Scheduler.AddTimer(owner, seconds, true, action);
}

// Posted continuations run in the frame's pump, on the main thread.
internal sealed class MainThreadContext : SynchronizationContext
{
    private readonly Queue<(SendOrPostCallback, object?)> _queue = new();
    private readonly object _lock = new();

    public override void Post(SendOrPostCallback d, object? state)
    {
        lock (_lock) _queue.Enqueue((d, state));
    }

    public override void Send(SendOrPostCallback d, object? state) => d(state);

    public void Pump()
    {
        // What a continuation posts runs this frame too, but a self-reposting loop can't hang it.
        for (int budget = 10000; budget > 0; --budget)
        {
            (SendOrPostCallback, object?) item;
            lock (_lock)
            {
                if (_queue.Count == 0) return;
                item = _queue.Dequeue();
            }
            try { item.Item1(item.Item2); }
            catch (Exception ex) { ScriptBridge.ReportHandlerException("async continuation", ex); }
        }
    }

    public void Clear()
    {
        lock (_lock) _queue.Clear();
    }
}

internal static class Scheduler
{
    private const float Epsilon = 1e-4f;

    private sealed class Job
    {
        public Script? Owner;
        public AsyncHandle Handle = new();
        /// <summary>Dt -&gt; finished</summary>
        public Func<float, bool> Step = _ => true;
    }

    internal static readonly MainThreadContext Context = new();
    private static readonly List<Job> Jobs = new();
    internal static long Frame;

    // Once a frame, before OnUpdate.
    internal static void InstallContext()
    {
        if (SynchronizationContext.Current != Context)
        {
            SynchronizationContext.SetSynchronizationContext(Context);
        }
    }

    internal static void Tick(float deltaTime)
    {
        Frame++;
        Context.Pump();
        // Jobs added while ticking start next frame.
        foreach (var job in Jobs.ToArray())
        {
            bool finished;
            try { finished = job.Handle.Cancelled || job.Step(deltaTime); }
            catch (Exception ex) { ScriptBridge.ReportHandlerException("coroutine, timer or tween", ex); finished = true; }
            if (finished)
            {
                Jobs.Remove(job);
                job.Handle.Done.TrySetResult(!job.Handle.Cancelled);
            }
        }
        // Continuations the finished jobs released run now, still this frame.
        Context.Pump();
    }

    internal static void RemoveOwner(object owner)
    {
        foreach (var job in Jobs)
        {
            if (ReferenceEquals(job.Owner, owner)) job.Handle.Cancel();
        }
    }

    internal static void Clear()
    {
        Jobs.Clear();
        Context.Clear();
        Frame = 0;
    }

    private static AsyncHandle Add(Script? owner, Func<float, bool> step)
    {
        var job = new Job { Owner = owner, Step = step };
        Jobs.Add(job);
        return job.Handle;
    }

    internal static Task Wait(Script? owner, float seconds, int frames, Func<bool>? condition)
    {
        float elapsed = 0f;
        int ticks = 0;
        return Add(owner, dt =>
        {
            elapsed += dt;
            ticks++;
            if (condition != null) return condition();
            return frames > 0 ? ticks >= frames : elapsed + Epsilon >= seconds;
        }).Task;
    }

    internal static AsyncHandle AddTimer(Script? owner, float seconds, bool repeat, Action action)
    {
        float elapsed = 0f;
        return Add(owner, dt =>
        {
            elapsed += dt;
            if (elapsed + Epsilon < seconds) return false;
            action();
            if (!repeat) return true;
            elapsed -= Math.Max(seconds, Epsilon);
            return false;
        });
    }

    internal static AsyncHandle AddTween(Script? owner, float seconds, Ease ease, Action<float> apply)
    {
        float elapsed = 0f;
        apply(Easing.Evaluate(ease, 0f));
        return Add(owner, dt =>
        {
            elapsed += dt;
            bool done = seconds <= 0f || elapsed + Epsilon >= seconds;
            apply(done ? 1f : Easing.Evaluate(ease, elapsed / seconds));
            return done;
        });
    }

    internal static AsyncHandle AddCoroutine(Script? owner, IEnumerator routine)
    {
        var stack = new Stack<IEnumerator>();
        stack.Push(routine);
        float wait = 0f;
        int waitFrames = 0;
        Func<bool>? until = null;
        return Add(owner, dt =>
        {
            if (wait > 0f) { wait -= dt; if (wait > Epsilon) return false; wait = 0f; }
            if (waitFrames > 0 && --waitFrames > 0) return false;
            if (until != null) { if (!until()) return false; until = null; }
            while (stack.Count > 0)
            {
                var top = stack.Peek();
                if (!top.MoveNext()) { stack.Pop(); continue; }
                switch (top.Current)
                {
                    case WaitForSeconds w: wait = w.Seconds; return false;
                    case WaitFrames f: waitFrames = f.Count; return false;
                    case WaitUntil u: until = u.Condition; return false;
                    case IEnumerator nested: stack.Push(nested); continue;
                    default: return false;   // null (or anything else): the next frame
                }
            }
            return true;
        });
    }
}
