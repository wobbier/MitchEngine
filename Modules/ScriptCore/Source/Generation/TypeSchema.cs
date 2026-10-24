using System;
using System.Collections.Generic;

namespace ScriptCore.Generation;

// What a generated field is, for save/net/inspector/UI layers that need to walk fields
// without reflection.
public enum FieldKind
{
    Bool,
    Int,
    Float,
    Double,
    String,
    Enum,
    Vector2,
    Vector3,
}

// One persisted field: its name, kind and boxed accessors (generated static lambdas, not
// reflection, so collectible script assemblies stay unloadable and AOT stays possible).
public sealed class FieldDescriptor
{
    public string Name { get; }
    public FieldKind Kind { get; }
    // Display name of the field's type ("float", "Vector3", "My.Enum").
    public string TypeName { get; }
    public Func<object, object?> GetValue { get; }
    public Action<object, object?> SetValue { get; }

    public FieldDescriptor(string name, FieldKind kind, string typeName,
        Func<object, object?> getValue, Action<object, object?> setValue)
    {
        Name = name;
        Kind = kind;
        TypeName = typeName;
        GetValue = getValue;
        SetValue = setValue;
    }
}

// A type's generated schema. Hash is a stable FNV-1a over the names and kinds, so two builds
// agree on it exactly when their fields agree (net handshakes, save versioning).
public sealed class TypeSchema
{
    public string TypeName { get; }
    public ulong Hash { get; }
    public IReadOnlyList<FieldDescriptor> Fields { get; }

    public TypeSchema(string typeName, ulong hash, FieldDescriptor[] fields)
    {
        TypeName = typeName;
        Hash = hash;
        Fields = fields;
    }
}
