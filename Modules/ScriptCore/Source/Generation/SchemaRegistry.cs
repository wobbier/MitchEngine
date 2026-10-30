using System;
using System.Runtime.CompilerServices;

namespace ScriptCore.Generation;

/// <summary>
/// Generated module initializers register each schema here, so lookups by type need no
/// reflection. Keys are held weakly: a hot-reloaded script assembly's types must stay
/// collectible, or the old AssemblyLoadContext could never unload.
/// </summary>
public static class SchemaRegistry
{
    private static readonly ConditionalWeakTable<Type, TypeSchema> s_schemas = new();

    public static void Register(Type type, TypeSchema schema)
    {
        s_schemas.AddOrUpdate(type, schema);
    }

    public static bool TryGet(Type type, out TypeSchema? schema)
    {
        return s_schemas.TryGetValue(type, out schema);
    }
}
