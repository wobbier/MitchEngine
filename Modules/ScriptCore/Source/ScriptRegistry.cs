using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace ScriptCore;

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

    // only used when I get to hot reloading
    public static void Clear() => _types.Clear();
}