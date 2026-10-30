using System;
using System.Collections.Generic;

namespace ScriptCore.Generation;

/// <summary>
/// What a generated field is, for save/net/inspector/UI layers that need to walk fields
/// without reflection.
/// </summary>
public enum FieldKind
{
    /// <summary>bool.</summary>
    Bool,
    /// <summary>int.</summary>
    Int,
    /// <summary>float.</summary>
    Float,
    /// <summary>double.</summary>
    Double,
    /// <summary>string.</summary>
    String,
    /// <summary>An enum (stored as its integer).</summary>
    Enum,
    /// <summary>Vector2.</summary>
    Vector2,
    /// <summary>Vector3.</summary>
    Vector3,
}

/// <summary>
/// One persisted field: its name, kind and boxed accessors (generated static lambdas, not
/// reflection, so collectible script assemblies stay unloadable and AOT stays possible).
/// </summary>
public sealed class FieldDescriptor
{
    /// <summary>The field's name.</summary>
    public string Name { get; }
    /// <summary>What kind of value it holds.</summary>
    public FieldKind Kind { get; }
    /// <summary>
    /// Display name of the field's type ("float", "Vector3", "My.Enum").
    /// </summary>
    public string TypeName { get; }
    /// <summary>Reads the field from an instance (boxed).</summary>
    public Func<object, object?> GetValue { get; }
    /// <summary>Writes the field on an instance (boxed).</summary>
    public Action<object, object?> SetValue { get; }

    /// <summary>A descriptor (generated code builds these).</summary>
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

/// <summary>
/// A type's generated schema. Hash is a stable FNV-1a over the names and kinds, so two builds
/// agree on it exactly when their fields agree (net handshakes, save versioning).
/// </summary>
public sealed class TypeSchema
{
    /// <summary>The type's full name.</summary>
    public string TypeName { get; }
    /// <summary>A hash of the field layout (save and network compatibility checks).</summary>
    public ulong Hash { get; }
    /// <summary>The persisted fields, in declaration order.</summary>
    public IReadOnlyList<FieldDescriptor> Fields { get; }

    /// <summary>A schema (generated code builds these).</summary>
    public TypeSchema(string typeName, ulong hash, FieldDescriptor[] fields)
    {
        TypeName = typeName;
        Hash = hash;
        Fields = fields;
    }
}
