using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;

namespace ScriptCore;

// PLAY-10: a developer console command. On a static method taking string[] (or nothing) and
// returning a string (printed) or nothing:
//   [ConsoleCommand("spawn", "Spawns a prefab: spawn <path>")]
//   public static string Spawn(string[] args) { World.Instantiate(args[0]); return "spawned"; }
// The engine's, the game's and the mods' assemblies are searched; ~ opens the console in game builds.
[AttributeUsage(AttributeTargets.Method)]
public sealed class ConsoleCommandAttribute : Attribute
{
    public ConsoleCommandAttribute(string name, string help = "")
    {
        Name = name;
        Help = help;
    }

    public string Name { get; }
    public string Help { get; }
}

public static class ConsoleCommands
{
    private static readonly Dictionary<string, (MethodInfo Method, string Help)> _commands = new(StringComparer.Ordinal);
    private static int _scannedVersion = -1;

    private static void Refresh()
    {
        if (_scannedVersion == ScriptRegistry.Version) return;
        _scannedVersion = ScriptRegistry.Version;
        _commands.Clear();
        // The engine's own (CS-12's game-tests) first, so a game command of the same name wins.
        foreach (var assembly in ScriptRegistry.Assemblies.Prepend(typeof(ConsoleCommands).Assembly).Distinct())
        {
            Type[] types;
            try { types = assembly.GetTypes(); }
            catch (ReflectionTypeLoadException ex) { types = ex.Types.Where(t => t != null).ToArray()!; }
            foreach (var type in types)
            {
                foreach (var method in type.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic))
                {
                    var attribute = method.GetCustomAttribute<ConsoleCommandAttribute>();
                    if (attribute == null) continue;
                    var parameters = method.GetParameters();
                    bool takesArgs = parameters.Length == 1 && parameters[0].ParameterType == typeof(string[]);
                    if (!(parameters.Length == 0 || takesArgs))
                    {
                        Console.WriteLine($"[Console] {type.Name}.{method.Name}: a [ConsoleCommand] takes string[] or nothing");
                        continue;
                    }
                    _commands[attribute.Name] = (method, attribute.Help);
                }
            }
        }
    }

    public static IEnumerable<(string Name, string Help)> List()
    {
        Refresh();
        return _commands.OrderBy(c => c.Key, StringComparer.Ordinal).Select(c => (c.Key, c.Value.Help));
    }

    // Null when no C# command has that name.
    public static string? Run(string name, string[] args)
    {
        Refresh();
        if (!_commands.TryGetValue(name, out var command)) return null;
        var parameters = command.Method.GetParameters().Length == 0 ? Array.Empty<object>() : new object[] { args };
        try
        {
            return command.Method.Invoke(null, parameters) as string ?? "";
        }
        catch (TargetInvocationException ex) when (ex.InnerException != null)
        {
            return $"{name} threw {ex.InnerException.GetType().Name}: {ex.InnerException.Message}";
        }
    }
}
