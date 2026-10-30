using Microsoft.CodeAnalysis;

namespace MitchEngine.Generators;

// NET-6: problems that stop the NetGenerator from implementing INetScript on a type. All are
// errors, and a type with any of them gets no generated code at all.
internal static class NetDiagnostics
{
    private const string Category = "MitchEngine.Networking";

    public static readonly DiagnosticDescriptor NotPartial = new(
        id: "MEGEN004",
        title: "Networked scripts must be partial",
        messageFormat: "'{0}' has networked members but is not declared partial, so nothing can be generated for it",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor NotScript = new(
        id: "MEGEN005",
        title: "Networked types must derive from ScriptCore.Script",
        messageFormat: "'{0}' has networked members but does not derive from ScriptCore.Script (RPCs and ownership need its Entity)",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor UnsupportedType = new(
        id: "MEGEN006",
        title: "Networked types must be non-generic, non-nested classes",
        messageFormat: "'{0}' has networked members but only non-generic, non-nested classes are supported",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor UnsupportedField = new(
        id: "MEGEN007",
        title: "Field can't be networked",
        messageFormat: "'{0}.{1}' can't be networked: {2}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor RpcShape = new(
        id: "MEGEN008",
        title: "RPCs must be declared as bodiless partial void methods",
        messageFormat: "RPC '{0}.{1}' {2}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor RpcParameter = new(
        id: "MEGEN009",
        title: "RPC parameter can't be sent",
        messageFormat: "RPC '{0}.{1}' parameter '{2}' can't be sent: {3}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor DerivedNetScript = new(
        id: "MEGEN011",
        title: "Networked scripts can't derive from another networked script",
        messageFormat: "'{0}' derives from networked script '{1}': networked members on both a base and a derived script aren't supported (keep them in one class)",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );

    public static readonly DiagnosticDescriptor MissingHandler = new(
        id: "MEGEN010",
        title: "RPC has no handler",
        messageFormat: "RPC '{0}.{1}' has no handler: add 'void On{1}({2})' to receive it",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true );
}
