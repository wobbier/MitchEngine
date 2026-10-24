using Microsoft.CodeAnalysis;

namespace MitchEngine.Generators;

internal static class SchemaDiagnostics
{
    private const string Category = "MitchEngine.Generation";

    public static readonly DiagnosticDescriptor NotPartial = new(
        id: "MEGEN001",
        title: "Schema types must be partial",
        messageFormat: "'{0}' has [GenerateSchema] but is not declared partial, so nothing can be generated for it",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor UnsupportedField = new(
        id: "MEGEN002",
        title: "Field not included in the generated schema",
        messageFormat: "'{0}.{1}' is skipped: {2}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Warning,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor UnsupportedType = new(
        id: "MEGEN003",
        title: "Schema types must be non-generic, non-nested classes",
        messageFormat: "'{0}' has [GenerateSchema] but only non-generic, non-nested classes are supported",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );
}
