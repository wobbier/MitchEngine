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

    // TODO: scripts are looked up by Type.Name so fix name clashing whenever
    public static IReadOnlyList<Type> ScriptTypes => _types;

    public static void Scan(Assembly assembly)
    {
        _types = assembly.GetTypes()
            .Where(t => typeof(IGameScript).IsAssignableFrom(t)
                     && !t.IsInterface
                     && !t.IsAbstract)
            .OrderBy(t => t.Name)
            .ToList();
    }

    // By class name, then by a [FormerName] of a renamed class.
    public static Type? Find(string name)
    {
        return _types.FirstOrDefault(t => t.Name == name)
            ?? _types.FirstOrDefault(t => t.GetCustomAttributes<FormerNameAttribute>(false).Any(a => a.Name == name));
    }

    // only used when I get to hot reloading
    public static void Clear() => _types.Clear();
}