using System;
using System.Reflection;
using System.Runtime.Loader;

namespace ScriptCore;

public sealed class GameScriptALC : AssemblyLoadContext
{
    private readonly AssemblyDependencyResolver _resolver;

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

        string? path = _resolver.ResolveAssemblyToPath(name);
        return path is not null ? LoadFromAssemblyPath(path) : null;
    }
}
