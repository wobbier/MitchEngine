using System;

namespace ScriptCore.Generation;

// Opts a partial class into the CS-9 source generator: a static TypeSchema, SaveFields/LoadFields
// JSON serializers and a SchemaRegistry entry are generated at compile time, with no reflection.
// The class must be a non-generic, non-nested partial class; unsupported fields are skipped with
// a MEGEN002 warning.
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class GenerateSchemaAttribute : Attribute
{
}
