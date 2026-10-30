using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace ScriptCore;

/// <summary>
/// Keeps saved data loading after a rename. On a script class, scenes that name the old class
/// create this one (and are saved with the new name); on a field, a value saved under the old name
/// is read into it.
///   [FormerName("EnemyBrain")] public class EnemyAI : Script { [FormerName("speed")] public float MoveSpeed; }
/// </summary>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Field, AllowMultiple = true)]
public sealed class FormerNameAttribute : Attribute
{
    /// <summary>Marks a script class with a name it used to have (scenes saved with the old name still load).</summary>
    public FormerNameAttribute(string name) => Name = name;
    /// <summary>The former name.</summary>
    public string Name { get; }
}

/// <summary>
/// Update order among scripts: lower runs first (default 0) in OnStart, OnFixedUpdate, OnUpdate and
/// OnLateUpdate; scripts with the same order run in the order they started.
///   [ExecutionOrder(-100)] public class InputReader : Script { ... }
/// </summary>
[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class ExecutionOrderAttribute : Attribute
{
    /// <summary>Orders the script's updates: lower runs first (default 0).</summary>
    public ExecutionOrderAttribute(int order) => Order = order;
    /// <summary>The order.</summary>
    public int Order { get; }
}

/// <summary>The script classes the game's and mods' assemblies define.</summary>
public static class ScriptRegistry
{
    private static List<Type> _types = new();
    private static readonly List<Assembly> _assemblies = new();
    /// <summary>
    /// The game's assembly and each mod's, and a counter bumped whenever they change (PLAY-10's
    /// console commands rescan when it moves).
    /// </summary>
    public static IReadOnlyList<Assembly> Assemblies => _assemblies;
    /// <summary>Bumped whenever the assemblies change.</summary>
    public static int Version { get; private set; }
    private static readonly Dictionary<string, int> _shortNames = new();
    private static readonly HashSet<string> _warnedAmbiguous = new();

    /// <summary>
    /// The game's script classes, then each mod's (MOD-2), then the engine's own (the GAME-7
    /// starter kit: Door, Pickup...), each group by name. A game class with the same name as an
    /// engine one wins.
    /// </summary>
    public static IReadOnlyList<Type> ScriptTypes => _types;
    private static List<Type>? _engineTypes;
    private static List<Type> EngineTypes => _engineTypes ??= ScriptsIn(typeof(ScriptRegistry).Assembly);

    private static List<Type> ScriptsIn(Assembly assembly) => assembly.GetTypes()
        .Where(t => typeof(IGameScript).IsAssignableFrom(t) && !t.IsInterface && !t.IsAbstract)
        .OrderBy(t => t.Name)
        .ToList();

    /// <summary>
    /// The game's assembly: replaces whatever was scanned.
    /// </summary>
    public static void Scan(Assembly assembly)
    {
        _types = ScriptsIn(assembly);
        _types.AddRange(EngineTypes);
        _assemblies.Clear();
        _assemblies.Add(assembly);
        ++Version;
        Recount();
    }

    /// <summary>
    /// A mod's assembly (MOD-2): its classes join the game's.
    /// </summary>
    public static void ScanMod(Assembly assembly)
    {
        _types.InsertRange(_types.Count - EngineTypes.Count, ScriptsIn(assembly));
        _assemblies.Add(assembly);
        ++Version;
        Recount();
    }

    private static void Recount()
    {
        _shortNames.Clear();
        foreach (var type in _types)
        {
            _shortNames[type.Name] = _shortNames.GetValueOrDefault(type.Name) + 1;
        }
    }

    /// <summary>
    /// What scenes and the editor call a script: its class name, or its full name ("ExampleMod.Spinner")
    /// when another script class has the same name.
    /// </summary>
    public static string NameOf(Type type) => _shortNames.GetValueOrDefault(type.Name) > 1 ? type.FullName ?? type.Name : type.Name;

    /// <summary>
    /// By full name; then by class name (when two classes share it, the game's wins and that's
    /// logged once); then by a [FormerName] of a renamed class.
    /// </summary>
    public static Type? Find(string name)
    {
        var exact = _types.FirstOrDefault(t => t.FullName == name);
        if (exact != null)
        {
            return exact;
        }
        var byName = _types.FirstOrDefault(t => t.Name == name);
        if (byName != null)
        {
            if (_shortNames.GetValueOrDefault(name) > 1 && _warnedAmbiguous.Add(name))
            {
                var all = string.Join(", ", _types.Where(t => t.Name == name).Select(t => t.FullName));
                var message = $"Scripts: '{name}' names more than one script class ({all}); using {byName.FullName}. Name the others by their full name.";
                unsafe
                {
                    if (Engine._api.LogWarning != null) Debug.Warning(message);
                    else Console.WriteLine(message);
                }
            }
            return byName;
        }
        return _types.FirstOrDefault(t => t.GetCustomAttributes<FormerNameAttribute>(false).Any(a => a.Name == name));
    }

    /// <summary>Forgets every type and assembly (before a reload).</summary>
    public static void Clear()
    {
        _types.Clear();
        _assemblies.Clear();
        ++Version;
        _shortNames.Clear();
        _warnedAmbiguous.Clear();
    }
}