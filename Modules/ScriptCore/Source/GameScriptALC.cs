using System;
using System.Reflection;
using System.Runtime.Loader;

namespace ScriptCore;

public sealed class GameScriptALC : AssemblyLoadContext
{
    private readonly AssemblyDependencyResolver _resolver;
    // For a mod's context: the game's Game.Script, which the mod resolves to instead of loading its own.
    public System.Reflection.Assembly? GameAssembly { get; init; }

    public GameScriptALC(string assemblyPath)
        : base($"GameScripts-{Guid.NewGuid():N}", isCollectible: true)
    {
        _resolver = new AssemblyDependencyResolver(assemblyPath);
    }

    protected override Assembly? Load(AssemblyName name)
    {
        if (name.Name == "ScriptCore")
        {
            return typeof(IGameScript).Assembly;
        }
        // A mod's context (MOD-2) shares the game's scripts, so mods can use the game's classes.
        if (GameAssembly != null && name.Name == GameAssembly.GetName().Name)
        {
            return GameAssembly;
        }

        string? path = _resolver.ResolveAssemblyToPath(name);
        return path is not null ? LoadFromAssemblyPath(path) : null;
    }
}
