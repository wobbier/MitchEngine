using System;
using System.Reflection;
using System.Runtime.Loader;

namespace ScriptCore;

/// <summary>The collectible load context game script assemblies live in, so a hot reload can unload them (host plumbing).</summary>
public sealed class GameScriptALC : AssemblyLoadContext
{
    private readonly AssemblyDependencyResolver _resolver;
    /// <summary>
    /// For a mod's context: the game's Game.Script, which the mod resolves to instead of loading its own.
    /// </summary>
    public System.Reflection.Assembly? GameAssembly { get; init; }

    /// <summary>A context for the assembly at assemblyPath.</summary>
    public GameScriptALC(string assemblyPath)
        : base($"GameScripts-{Guid.NewGuid():N}", isCollectible: true)
    {
        _resolver = new AssemblyDependencyResolver(assemblyPath);
    }

    /// <inheritdoc/>
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
