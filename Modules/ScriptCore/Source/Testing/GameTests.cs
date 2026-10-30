using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;

namespace ScriptCore;

// CS-12: play-mode tests. A static method returning Task (or void), in the game's or a mod's scripts:
//
//   [GameTest("Assets/Scenes/Main.lvl")]
//   public static async Task Medkit_heals()
//   {
//       var player = World.Find("Player");
//       await GameAssert.Eventually(() => player.GetScript<IHealth>()!.Current > 50f, 5f, "healed");
//   }
//
// They run in Play, one after another: each loads its scene (when it names one) and waits for it,
// then runs with a timeout. "game-tests [filter]" in the console (or an edscript's console line)
// starts them; each logs [GameTest] PASS/FAIL, and the run ends with
// "GameTests: N passed, M failed" (flows wait for it; RunChecks plays the templates' tests).
[AttributeUsage(AttributeTargets.Method)]
public sealed class GameTestAttribute : Attribute
{
    public GameTestAttribute(string scene = "") { Scene = scene; }

    /// <summary>
    /// Loaded before the test; empty runs it in whatever scene is playing.
    /// </summary>
    public string Scene { get; }
    public float TimeoutSeconds { get; set; } = 30f;
}

public sealed class GameTestFailure : Exception
{
    public GameTestFailure(string message) : base(message) { }
}

public static class GameAssert
{
    public static void True(bool condition, string message = "expected true")
    {
        if (!condition) throw new GameTestFailure(message);
    }

    public static void Equal<T>(T expected, T actual, string what = "value")
    {
        if (!EqualityComparer<T>.Default.Equals(expected, actual)) throw new GameTestFailure($"{what}: expected {expected}, got {actual}");
    }

    public static void Near(float expected, float actual, float tolerance, string what = "value")
    {
        if (!(MathF.Abs(expected - actual) <= tolerance)) throw new GameTestFailure($"{what}: expected {expected} ± {tolerance}, got {actual}");
    }

    public static void Near(Vector3 expected, Vector3 actual, float tolerance, string what = "position")
    {
        if (!((expected - actual).Length() <= tolerance)) throw new GameTestFailure($"{what}: expected {expected} ± {tolerance}, got {actual}");
    }

    /// <summary>
    /// Waits (in game time) until the condition holds; fails after the timeout.
    /// </summary>
    public static async Task Eventually(Func<bool> condition, float seconds, string what = "condition")
    {
        float waited = 0f;
        while (!condition())
        {
            if (waited >= seconds) throw new GameTestFailure($"{what}: not true after {seconds} s");
            waited += Time.DeltaTime;
            await Async.NextFrame();
        }
    }
}

public static class GameTests
{
    public readonly record struct Result(string Name, bool Passed, string Message, double Seconds);

    public static bool IsRunning { get; private set; }
    public static IReadOnlyList<Result> LastResults => _results;
    private static readonly List<Result> _results = new();

    /// <summary>
    /// Every [GameTest] in the game's and mods' scripts, by class then method name.
    /// </summary>
    public static List<MethodInfo> Discover(string filter = "")
    {
        var tests = new List<MethodInfo>();
        foreach (var assembly in ScriptRegistry.Assemblies)
        {
            Type[] types;
            try { types = assembly.GetTypes(); }
            catch (ReflectionTypeLoadException ex) { types = ex.Types.Where(t => t != null).ToArray()!; }
            foreach (var type in types.OrderBy(t => t.FullName, StringComparer.Ordinal))
            {
                foreach (var method in type.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic).OrderBy(m => m.Name, StringComparer.Ordinal))
                {
                    if (method.GetCustomAttribute<GameTestAttribute>() == null) continue;
                    if (method.GetParameters().Length != 0 || (method.ReturnType != typeof(Task) && method.ReturnType != typeof(void)))
                    {
                        Console.WriteLine($"[GameTest] {type.Name}.{method.Name}: a [GameTest] is a static method with no parameters returning Task or void");
                        continue;
                    }
                    if (filter.Length > 0 && !NameOf(method).Contains(filter, StringComparison.OrdinalIgnoreCase)) continue;
                    tests.Add(method);
                }
            }
        }
        return tests;
    }

    public static string NameOf(MethodInfo method) => $"{method.DeclaringType?.Name}.{method.Name}";

    [ConsoleCommand("game-tests", "Runs the [GameTest] play-mode tests: game-tests [name filter]")]
    public static string RunCommand(string[] args)
    {
        if (IsRunning) return "game tests are already running";
        var tests = Discover(args.Length > 0 ? string.Join(" ", args) : "");
        if (tests.Count == 0)
        {
            Debug.Log("GameTests: 0 passed, 0 failed (no tests)");
            return "no [GameTest] methods match";
        }
        _ = RunAll(tests);
        return $"running {tests.Count} game test(s)";
    }

    public static async Task RunAll(IReadOnlyList<MethodInfo> tests)
    {
        IsRunning = true;
        _results.Clear();
        try
        {
            foreach (var method in tests) _results.Add(await RunOne(method));
        }
        finally
        {
            IsRunning = false;
        }
        int passed = _results.Count(r => r.Passed);
        Debug.Log($"GameTests: {passed} passed, {_results.Count - passed} failed");
    }

    private static async Task<Result> RunOne(MethodInfo method)
    {
        var attribute = method.GetCustomAttribute<GameTestAttribute>()!;
        string name = NameOf(method);
        var clock = Stopwatch.StartNew();
        string failure = "";
        try
        {
            if (attribute.Scene.Length > 0) await LoadScene(attribute.Scene);
            var run = method.Invoke(null, null) as Task ?? Task.CompletedTask;
            // Game time, so a paused or slow frame doesn't fail a test early.
            float waited = 0f;
            while (!run.IsCompleted && waited < attribute.TimeoutSeconds)
            {
                waited += Time.DeltaTime;
                await Async.NextFrame();
            }
            if (!run.IsCompleted) failure = $"timed out after {attribute.TimeoutSeconds} s";
            else if (run.Exception != null) failure = Describe(run.Exception.InnerException ?? run.Exception);
        }
        catch (TargetInvocationException ex) { failure = Describe(ex.InnerException ?? ex); }
        catch (Exception ex) { failure = Describe(ex); }
        // A test's simulated keys don't outlive it, even when it fails mid-hold.
        Input.ReleaseSimulated();
        var result = new Result(name, failure.Length == 0, failure, clock.Elapsed.TotalSeconds);
        Debug.Log(result.Passed ? $"[GameTest] PASS {name} ({result.Seconds:0.0} s)" : $"[GameTest] FAIL {name}: {failure}");
        return result;
    }

    private static string Describe(Exception ex) => ex is GameTestFailure ? ex.Message : $"{ex.GetType().Name}: {ex.Message}";

    private static async Task LoadScene(string scene)
    {
        bool loaded = false;
        using var subscription = EventBus.Subscribe<SceneLoaded>(e => { if (SamePath(e.Path, scene)) loaded = true; });
        World.LoadScene(scene);
        float waited = 0f;
        while (!loaded)
        {
            if (waited > 30f) throw new GameTestFailure($"scene {scene} didn't load");
            waited += MathF.Max(Time.DeltaTime, 1f / 60f);
            await Async.NextFrame();
        }
        // Its scripts start this frame; give them their OnStart.
        await Async.NextFrame();
    }

    private static bool SamePath(string a, string b) => string.Equals(a.Replace('\\', '/'), b.Replace('\\', '/'), StringComparison.OrdinalIgnoreCase);
}
