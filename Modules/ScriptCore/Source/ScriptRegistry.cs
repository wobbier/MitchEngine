using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace ScriptCore;

// Keeps saved data loading after a rename. On a script class, scenes that name the old class
// create this one (and are saved with the new name); on a field, a value saved under the old name
// is read into it.
//   [FormerName("EnemyBrain")] public class EnemyAI : Script { [FormerName("speed")] public float MoveSpeed; }
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Field, AllowMultiple = true)]
public sealed class FormerNameAttribute : Attribute
{
    public FormerNameAttribute(string name) => Name = name;
    public string Name { get; }
}

// Update order among scripts: lower runs first (default 0) in OnStart, OnFixedUpdate, OnUpdate and
// OnLateUpdate; scripts with the same order run in the order they started.
//   [ExecutionOrder(-100)] public class InputReader : Script { ... }
[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class ExecutionOrderAttribute : Attribute
{
    public ExecutionOrderAttribute(int order) => Order = order;
    public int Order { get; }
}

public static class ScriptRegistry
{
    private static List<Type> _types = new();
    private static readonly Dictionary<string, int> _shortNames = new();
    private static readonly HashSet<string> _warnedAmbiguous = new();

    // The game's script classes, then each mod's (MOD-2), each group by name.
    public static IReadOnlyList<Type> ScriptTypes => _types;

    private static List<Type> ScriptsIn(Assembly assembly) => assembly.GetTypes()
        .Where(t => typeof(IGameScript).IsAssignableFrom(t) && !t.IsInterface && !t.IsAbstract)
        .OrderBy(t => t.Name)
        .ToList();

    // The game's assembly: replaces whatever was scanned.
    public static void Scan(Assembly assembly)
    {
        _types = ScriptsIn(assembly);
        Recount();
    }

    // A mod's assembly (MOD-2): its classes join the game's.
    public static void ScanMod(Assembly assembly)
    {
        _types.AddRange(ScriptsIn(assembly));
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

    // What scenes and the editor call a script: its class name, or its full name ("ExampleMod.Spinner")
    // when another script class has the same name.
    public static string NameOf(Type type) => _shortNames.GetValueOrDefault(type.Name) > 1 ? type.FullName ?? type.Name : type.Name;

    // By full name; then by class name (when two classes share it, the game's wins and that's
    // logged once); then by a [FormerName] of a renamed class.
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

    public static void Clear()
    {
        _types.Clear();
        _shortNames.Clear();
        _warnedAmbiguous.Clear();
    }
}