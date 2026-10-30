using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace MitchEngine.Generators;

internal enum NetKind
{
    Bool,
    Int,
    Float,
    Double,
    Enum,
    Vector2,
    Vector3,
    Quaternion,
    // RPC arguments only.
    String,
    Entity,
}

internal sealed class NetFieldModel
{
    public string Name = "";
    public NetKind Kind;
    public string CastType = "";
    public bool Predicted;
}

internal sealed class NetParameterModel
{
    public string Name = "";
    public NetKind Kind;
    public string TypeRef = "";
}

internal sealed class NetRpcModel
{
    public string Name = "";
    public bool Server;
    public bool Owner;
    // Accessibility keywords of the user's declaration; the implementation must repeat them.
    public string Modifiers = "";
    public List<NetParameterModel> Parameters = new();
}

internal sealed class NetTypeModel
{
    public string? Namespace;
    public string TypeName = "";
    public string FullName = "";
    public List<NetFieldModel> Fields = new();
    // In ordinal name order: the index is the RPC id.
    public List<NetRpcModel> Rpcs = new();
    public List<Diagnostic> Diagnostics = new();
    public bool Emit;
    // ME-294: the nearest base script with net members (fully qualified), whose layers come first.
    public string? NetBase;
    public bool IsSealed;
}

// NET-6: for every partial Script with [Replicated] / [Predicted] fields or [ServerRpc] / [ClientRpc]
// methods, implements ScriptCore.INetScript: fixed-size little-endian state for snapshots and
// rollback, RPC send stubs and the dispatch switch, plus __NetSchemaHash for the handshake.
// Zero allocation and no reflection; see Engine/Docs/Networking.md.
[Generator( LanguageNames.CSharp )]
public sealed class NetGenerator : IIncrementalGenerator
{
    public const string ReplicatedMetadataName = "ScriptCore.ReplicatedAttribute";
    public const string PredictedMetadataName = "ScriptCore.PredictedAttribute";
    public const string ServerRpcMetadataName = "ScriptCore.ServerRpcAttribute";
    public const string ClientRpcMetadataName = "ScriptCore.ClientRpcAttribute";
    private const string ScriptMetadataName = "ScriptCore.Script";
    private const string EntityTypeRef = "global::ScriptCore.Entity";
    // String RPCs serialize into a stack buffer of at least this size (about 1000 bytes of text).
    // The RPC message limit (1024 bytes) less its worst-case header (type, NetId, id, size).
    private const int StringRpcBufferSize = 1008;

    private static readonly HashSet<string> AttributeNames = new()
    {
        "Replicated", "ReplicatedAttribute", "Predicted", "PredictedAttribute",
        "ServerRpc", "ServerRpcAttribute", "ClientRpc", "ClientRpcAttribute",
    };

    private static readonly SymbolDisplayFormat TypeFormat = SymbolDisplayFormat.FullyQualifiedFormat
        .AddMiscellaneousOptions( SymbolDisplayMiscellaneousOptions.IncludeNullableReferenceTypeModifier );

    public void Initialize( IncrementalGeneratorInitializationContext context )
    {
        var types = context.SyntaxProvider.CreateSyntaxProvider(
            static ( node, _ ) => node is TypeDeclarationSyntax type && HasNetAttributeSyntax( type ),
            static ( ctx, ct ) => BuildModel( ctx, ct ) )
            .Where( static model => model is not null );
        context.RegisterSourceOutput( types, static ( spc, model ) => Emit( spc, model! ) );
    }


    // Cheap syntax filter: a member carries an attribute named like one of ours.
    private static bool HasNetAttributeSyntax( TypeDeclarationSyntax type )
    {
        foreach( var member in type.Members )
        {
            if( member is not FieldDeclarationSyntax && member is not MethodDeclarationSyntax )
            {
                continue;
            }
            foreach( var list in member.AttributeLists )
            {
                foreach( var attribute in list.Attributes )
                {
                    var name = attribute.Name switch
                    {
                        QualifiedNameSyntax qualified => qualified.Right.Identifier.Text,
                        AliasQualifiedNameSyntax alias => alias.Name.Identifier.Text,
                        SimpleNameSyntax simple => simple.Identifier.Text,
                        _ => "",
                    };
                    if( AttributeNames.Contains( name ) )
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }


    private static bool HasAttribute( ISymbol symbol, string metadataName, out AttributeData? data )
    {
        foreach( var attribute in symbol.GetAttributes() )
        {
            if( attribute.AttributeClass?.ToDisplayString() == metadataName )
            {
                data = attribute;
                return true;
            }
        }
        data = null;
        return false;
    }


    private static NetTypeModel? BuildModel( GeneratorSyntaxContext ctx, CancellationToken ct )
    {
        var syntax = (TypeDeclarationSyntax)ctx.Node;
        if( ctx.SemanticModel.GetDeclaredSymbol( syntax, ct ) is not INamedTypeSymbol symbol )
        {
            return null;
        }

        // A partial type is built once, from the first of its declarations that has net members.
        foreach( var reference in symbol.DeclaringSyntaxReferences )
        {
            if( reference.GetSyntax( ct ) is TypeDeclarationSyntax declaration && HasNetAttributeSyntax( declaration ) )
            {
                if( reference.SyntaxTree != syntax.SyntaxTree || reference.Span != syntax.Span )
                {
                    return null;
                }
                break;
            }
        }

        var netFields = new List<IFieldSymbol>();
        var netMethods = new List<IMethodSymbol>();
        foreach( var member in symbol.GetMembers() )
        {
            if( member is IFieldSymbol field
                && ( HasAttribute( field, ReplicatedMetadataName, out _ ) || HasAttribute( field, PredictedMetadataName, out _ ) ) )
            {
                netFields.Add( field );
            }
            else if( member is IMethodSymbol method
                && ( HasAttribute( method, ServerRpcMetadataName, out _ ) || HasAttribute( method, ClientRpcMetadataName, out _ ) ) )
            {
                netMethods.Add( method );
            }
        }
        if( netFields.Count == 0 && netMethods.Count == 0 )
        {
            // Same-named attributes from somewhere else.
            return null;
        }

        var location = syntax.Identifier.GetLocation();
        var model = new NetTypeModel
        {
            Namespace = symbol.ContainingNamespace is { IsGlobalNamespace: false } ns ? ns.ToDisplayString() : null,
            TypeName = symbol.Name,
        };
        model.FullName = model.Namespace is null ? model.TypeName : model.Namespace + "." + model.TypeName;

        if( symbol.TypeKind != TypeKind.Class || symbol.ContainingType is not null || symbol.Arity > 0 )
        {
            model.Diagnostics.Add( Diagnostic.Create( NetDiagnostics.UnsupportedType, location, model.FullName ) );
            return model;
        }
        if( !syntax.Modifiers.Any( SyntaxKind.PartialKeyword ) )
        {
            model.Diagnostics.Add( Diagnostic.Create( NetDiagnostics.NotPartial, location, model.FullName ) );
            return model;
        }
        if( !DerivesFromScript( symbol ) )
        {
            model.Diagnostics.Add( Diagnostic.Create( NetDiagnostics.NotScript, location, model.FullName ) );
            return model;
        }
        // ME-294: a base script's net members come first; this class's layer is chained after them
        // (state appended, RPC ids offset by the base's count).
        model.IsSealed = symbol.IsSealed;
        var baseRpcNames = new HashSet<string>();
        for( var baseType = symbol.BaseType; baseType is not null && baseType.ToDisplayString() != ScriptMetadataName; baseType = baseType.BaseType )
        {
            if( HasNetMembers( baseType ) )
            {
                model.NetBase ??= baseType.ToDisplayString( SymbolDisplayFormat.FullyQualifiedFormat );
                foreach( var method in baseType.GetMembers().OfType<IMethodSymbol>() )
                {
                    if( HasAttribute( method, ServerRpcMetadataName, out _ ) || HasAttribute( method, ClientRpcMetadataName, out _ ) )
                    {
                        baseRpcNames.Add( method.Name );
                    }
                }
            }
        }

        bool ok = true;
        foreach( var field in netFields )
        {
            var fieldLocation = field.Locations.FirstOrDefault() ?? location;
            string? problem = null;
            NetKind kind = default;
            string castType = "";
            if( field.IsStatic || field.IsConst )
            {
                problem = "static fields aren't per-entity state";
            }
            else if( field.IsReadOnly )
            {
                problem = "readonly fields can't be restored";
            }
            else if( !TryMapKind( field.Type, allowRpcKinds: false, out kind, out castType ) )
            {
                problem = $"fields of type '{field.Type.ToDisplayString()}' aren't supported (bool, int, float, double, int enums, Vector2, Vector3, Quaternion)";
            }
            if( problem is not null )
            {
                model.Diagnostics.Add( Diagnostic.Create( NetDiagnostics.UnsupportedField, fieldLocation, model.FullName, field.Name, problem ) );
                ok = false;
                continue;
            }
            model.Fields.Add( new NetFieldModel
            {
                Name = field.Name,
                Kind = kind,
                CastType = castType,
                Predicted = HasAttribute( field, PredictedMetadataName, out _ ),
            } );
        }

        var rpcNames = new HashSet<string>();
        foreach( var method in netMethods )
        {
            var methodLocation = method.Locations.FirstOrDefault() ?? location;
            bool isServer = HasAttribute( method, ServerRpcMetadataName, out _ );
            bool isClient = HasAttribute( method, ClientRpcMetadataName, out var clientAttribute );

            string? problem = null;
            if( isServer && isClient )
            {
                problem = "can't be both a [ServerRpc] and a [ClientRpc]";
            }
            else if( !rpcNames.Add( method.Name ) )
            {
                problem = "is declared more than once (RPC names must be unique)";
            }
            else if( baseRpcNames.Contains( method.Name ) )
            {
                problem = "is already an RPC on a base script (RPC names must be unique across the hierarchy)";
            }
            else if( !method.IsPartialDefinition || method.PartialImplementationPart is not null
                || !method.ReturnsVoid || method.IsStatic || method.IsGenericMethod )
            {
                problem = $"must be declared as 'partial void {method.Name}(...);' without a body (the body is generated)";
            }
            if( problem is not null )
            {
                model.Diagnostics.Add( Diagnostic.Create( NetDiagnostics.RpcShape, methodLocation, model.FullName, method.Name, problem ) );
                ok = false;
                continue;
            }

            var rpc = new NetRpcModel
            {
                Name = method.Name,
                Server = isServer,
                Owner = isClient && clientAttribute!.ConstructorArguments.Length > 0
                    && clientAttribute.ConstructorArguments[0].Value is int target && target == 1,
                Modifiers = AccessibilityModifiers( method ),
            };
            bool parametersOk = true;
            foreach( var parameter in method.Parameters )
            {
                string? parameterProblem = null;
                NetKind kind = default;
                string typeRef = "";
                if( parameter.RefKind != RefKind.None || parameter.IsParams )
                {
                    parameterProblem = "ref, out, in and params parameters aren't supported";
                }
                else if( !TryMapKind( parameter.Type, allowRpcKinds: true, out kind, out typeRef ) )
                {
                    parameterProblem = $"type '{parameter.Type.ToDisplayString()}' isn't supported (bool, int, float, double, int enums, Vector2, Vector3, Quaternion, string, Entity)";
                }
                if( parameterProblem is not null )
                {
                    model.Diagnostics.Add( Diagnostic.Create( NetDiagnostics.RpcParameter, parameter.Locations.FirstOrDefault() ?? methodLocation,
                        model.FullName, method.Name, parameter.Name, parameterProblem ) );
                    parametersOk = false;
                    continue;
                }
                rpc.Parameters.Add( new NetParameterModel
                {
                    Name = EscapeIdentifier( parameter.Name ),
                    Kind = kind,
                    TypeRef = parameter.Type.ToDisplayString( TypeFormat ),
                } );
            }
            if( !parametersOk )
            {
                ok = false;
                continue;
            }

            if( !HasHandler( symbol, method ) )
            {
                var signature = string.Join( ", ", method.Parameters.Select( p => p.Type.ToDisplayString() + " " + p.Name ) );
                model.Diagnostics.Add( Diagnostic.Create( NetDiagnostics.MissingHandler, methodLocation, model.FullName, method.Name, signature ) );
                ok = false;
                continue;
            }
            model.Rpcs.Add( rpc );
        }
        model.Rpcs.Sort( ( a, b ) => string.CompareOrdinal( a.Name, b.Name ) );

        model.Emit = ok;
        return model;
    }


    private static bool HasNetMembers( INamedTypeSymbol type )
    {
        foreach( var member in type.GetMembers() )
        {
            if( member is IFieldSymbol field && ( HasAttribute( field, ReplicatedMetadataName, out _ ) || HasAttribute( field, PredictedMetadataName, out _ ) ) )
            {
                return true;
            }
            if( member is IMethodSymbol method && ( HasAttribute( method, ServerRpcMetadataName, out _ ) || HasAttribute( method, ClientRpcMetadataName, out _ ) ) )
            {
                return true;
            }
        }
        return false;
    }


    private static bool DerivesFromScript( INamedTypeSymbol symbol )
    {
        for( var type = symbol.BaseType; type is not null; type = type.BaseType )
        {
            if( type.ToDisplayString() == ScriptMetadataName )
            {
                return true;
            }
        }
        return false;
    }


    // On{Name} with the same parameter types, on the type or (non-private) on a base.
    private static bool HasHandler( INamedTypeSymbol symbol, IMethodSymbol rpc )
    {
        for( var type = symbol; type is not null; type = type.BaseType )
        {
            foreach( var candidate in type.GetMembers( "On" + rpc.Name ).OfType<IMethodSymbol>() )
            {
                if( candidate.IsStatic || candidate.Parameters.Length != rpc.Parameters.Length
                    || ( !SymbolEqualityComparer.Default.Equals( type, symbol ) && candidate.DeclaredAccessibility == Accessibility.Private ) )
                {
                    continue;
                }
                bool matches = true;
                for( int i = 0; i < rpc.Parameters.Length && matches; ++i )
                {
                    matches = candidate.Parameters[i].RefKind == RefKind.None
                        && SymbolEqualityComparer.Default.Equals( candidate.Parameters[i].Type, rpc.Parameters[i].Type );
                }
                if( matches )
                {
                    return true;
                }
            }
        }
        return false;
    }


    private static string AccessibilityModifiers( IMethodSymbol method )
    {
        foreach( var reference in method.DeclaringSyntaxReferences )
        {
            if( reference.GetSyntax() is MethodDeclarationSyntax declaration )
            {
                var keywords = declaration.Modifiers
                    .Where( m => m.IsKind( SyntaxKind.PublicKeyword ) || m.IsKind( SyntaxKind.PrivateKeyword )
                        || m.IsKind( SyntaxKind.ProtectedKeyword ) || m.IsKind( SyntaxKind.InternalKeyword ) )
                    .Select( m => m.Text );
                return string.Join( " ", keywords );
            }
        }
        return "";
    }


    private static string EscapeIdentifier( string name )
        => SyntaxFacts.GetKeywordKind( name ) != SyntaxKind.None ? "@" + name : name;


    private static bool TryMapKind( ITypeSymbol type, bool allowRpcKinds, out NetKind kind, out string castType )
    {
        castType = type.ToDisplayString( SymbolDisplayFormat.FullyQualifiedFormat );
        switch( type.SpecialType )
        {
            case SpecialType.System_Boolean: kind = NetKind.Bool; return true;
            case SpecialType.System_Int32: kind = NetKind.Int; return true;
            case SpecialType.System_Single: kind = NetKind.Float; return true;
            case SpecialType.System_Double: kind = NetKind.Double; return true;
            case SpecialType.System_String: kind = NetKind.String; return allowRpcKinds;
        }

        if( type.TypeKind == TypeKind.Enum )
        {
            kind = NetKind.Enum;
            return type is INamedTypeSymbol named && named.EnumUnderlyingType?.SpecialType == SpecialType.System_Int32;
        }

        switch( castType )
        {
            case "global::Vector2": kind = NetKind.Vector2; return true;
            case "global::Vector3": kind = NetKind.Vector3; return true;
            case "global::Quaternion": kind = NetKind.Quaternion; return true;
            case EntityTypeRef: kind = NetKind.Entity; return allowRpcKinds;
        }
        kind = default;
        return false;
    }


    // Bytes on the wire; strings are variable (their 4-byte length prefix is counted here).
    private static int SizeOf( NetKind kind ) => kind switch
    {
        NetKind.Bool => 1,
        NetKind.Double => 8,
        NetKind.Vector2 => 8,
        NetKind.Vector3 => 12,
        NetKind.Quaternion => 16,
        _ => 4,
    };


    private static string WriteCall( string writer, NetKind kind, string value ) => kind switch
    {
        NetKind.Enum => $"{writer}.WriteInt( (int){value} );",
        _ => $"{writer}.Write{kind}( {value} );",
    };


    private static string ReadCall( string reader, NetKind kind, string castType ) => kind switch
    {
        NetKind.Enum => $"({castType}){reader}.ReadInt()",
        _ => $"{reader}.Read{kind}()",
    };


    private static ulong Fnv1a64( string text )
    {
        ulong hash = 14695981039346656037UL;
        foreach( var b in Encoding.UTF8.GetBytes( text ) )
        {
            hash ^= b;
            hash *= 1099511628211UL;
        }
        return hash;
    }


    internal static ulong SchemaHash( NetTypeModel model )
    {
        // A derived layer's hash also folds in its base's (in the generated constant expression).
        var hashInput = new StringBuilder( model.FullName );
        foreach( var field in model.Fields )
        {
            hashInput.Append( "|F:" ).Append( field.Name ).Append( ':' ).Append( field.Kind ).Append( ':' ).Append( field.Predicted ? 'P' : 'R' );
        }
        foreach( var rpc in model.Rpcs )
        {
            hashInput.Append( "|M:" ).Append( rpc.Name ).Append( '(' )
                .Append( string.Join( ",", rpc.Parameters.Select( p => p.Kind.ToString() ) ) )
                .Append( "):" ).Append( rpc.Server ? 'S' : 'C' ).Append( ':' ).Append( rpc.Owner ? "Owner" : "All" );
        }
        return Fnv1a64( hashInput.ToString() );
    }


    private static void Emit( SourceProductionContext spc, NetTypeModel model )
    {
        foreach( var diagnostic in model.Diagnostics )
        {
            spc.ReportDiagnostic( diagnostic );
        }
        if( !model.Emit )
        {
            return;
        }

        var predicted = model.Fields.Where( f => f.Predicted ).ToList();
        int predictedSize = predicted.Sum( f => SizeOf( f.Kind ) );
        int replicatedSize = model.Fields.Sum( f => SizeOf( f.Kind ) );
        int flags = ( predicted.Count > 0 ? 1 : 0 ) | ( model.Fields.Count > 0 ? 2 : 0 ) | ( model.Rpcs.Count > 0 ? 4 : 0 );
        bool derived = model.NetBase is not null;
        // The layer methods: overridden by derived net scripts (ME-294); a sealed root needs no virtuals.
        var layer = derived ? "protected override" : model.IsSealed ? "private" : "protected virtual";

        var sb = new StringBuilder();
        sb.AppendLine( "// <auto-generated by MitchEngine.Generators (NET-6); do not edit.>" );
        sb.AppendLine( "#nullable enable" );
        sb.AppendLine( "#pragma warning disable" );
        sb.AppendLine();
        var indent = "";
        if( model.Namespace is not null )
        {
            sb.AppendLine( $"namespace {model.Namespace}" );
            sb.AppendLine( "{" );
            indent = "    ";
        }

        sb.AppendLine( derived
            ? $"{indent}partial class {EscapeIdentifier( model.TypeName )}"
            : $"{indent}partial class {EscapeIdentifier( model.TypeName )} : global::ScriptCore.INetScript" );
        sb.AppendLine( $"{indent}{{" );
        sb.AppendLine( $"{indent}    /// <summary>Generated net layout hash (NET-6): peers must agree on it in the session handshake.</summary>" );
        sb.AppendLine( derived
            ? $"{indent}    public new const ulong __NetSchemaHash = unchecked( 0x{SchemaHash( model ):X16}UL ^ ( {model.NetBase}.__NetSchemaHash * 1099511628211UL ) );"
            : $"{indent}    public const ulong __NetSchemaHash = 0x{SchemaHash( model ):X16}UL;" );
        sb.AppendLine();

        // Sizes and counts through the hierarchy.
        string Chain( string member, string mine, string op ) => derived ? $"base.{member} {op} {mine}" : mine;
        sb.AppendLine( $"{indent}    {layer} int __NetFlags => {Chain( "__NetFlags", flags.ToString(), "|" )};" );
        sb.AppendLine( $"{indent}    {layer} int __NetPredictedSize => {Chain( "__NetPredictedSize", predictedSize.ToString(), "+" )};" );
        sb.AppendLine( $"{indent}    {layer} int __NetReplicatedSize => {Chain( "__NetReplicatedSize", replicatedSize.ToString(), "+" )};" );
        sb.AppendLine( $"{indent}    {layer} int __NetRpcCount => {Chain( "__NetRpcCount", model.Rpcs.Count.ToString(), "+" )};" );
        sb.AppendLine();

        // Predicted state, this layer's fields after the base's: saved before and restored on a rollback.
        sb.AppendLine( $"{indent}    {layer} void __NetSavePredictedLayer( ref global::ScriptCore.NetWriter writer )" );
        sb.AppendLine( $"{indent}    {{" );
        if( derived )
        {
            sb.AppendLine( $"{indent}        base.__NetSavePredictedLayer( ref writer );" );
        }
        foreach( var field in predicted )
        {
            sb.AppendLine( $"{indent}        {WriteCall( "writer", field.Kind, "this." + EscapeIdentifier( field.Name ) )}" );
        }
        sb.AppendLine( $"{indent}    }}" );
        sb.AppendLine();
        sb.AppendLine( $"{indent}    {layer} void __NetRestorePredictedLayer( ref global::ScriptCore.NetReader reader )" );
        sb.AppendLine( $"{indent}    {{" );
        if( derived )
        {
            sb.AppendLine( $"{indent}        base.__NetRestorePredictedLayer( ref reader );" );
        }
        foreach( var field in predicted )
        {
            sb.AppendLine( $"{indent}        this.{EscapeIdentifier( field.Name )} = {ReadCall( "reader", field.Kind, field.CastType )};" );
        }
        sb.AppendLine( $"{indent}    }}" );
        sb.AppendLine();

        // Replicated state: every net field in declaration order, predicted ones included.
        sb.AppendLine( $"{indent}    {layer} void __NetWriteReplicatedLayer( ref global::ScriptCore.NetWriter writer )" );
        sb.AppendLine( $"{indent}    {{" );
        if( derived )
        {
            sb.AppendLine( $"{indent}        base.__NetWriteReplicatedLayer( ref writer );" );
        }
        foreach( var field in model.Fields )
        {
            sb.AppendLine( $"{indent}        {WriteCall( "writer", field.Kind, "this." + EscapeIdentifier( field.Name ) )}" );
        }
        sb.AppendLine( $"{indent}    }}" );
        sb.AppendLine();
        sb.AppendLine( $"{indent}    {layer} void __NetReadReplicatedLayer( ref global::ScriptCore.NetReader reader, bool skipPredicted )" );
        sb.AppendLine( $"{indent}    {{" );
        if( derived )
        {
            sb.AppendLine( $"{indent}        base.__NetReadReplicatedLayer( ref reader, skipPredicted );" );
        }
        foreach( var field in model.Fields )
        {
            var read = $"this.{EscapeIdentifier( field.Name )} = {ReadCall( "reader", field.Kind, field.CastType )};";
            sb.AppendLine( field.Predicted
                ? $"{indent}        if( skipPredicted ) {{ reader.Skip( {SizeOf( field.Kind )} ); }} else {{ {read} }}"
                : $"{indent}        {read}" );
        }
        sb.AppendLine( $"{indent}    }}" );
        sb.AppendLine();

        // RPCs: ids are the base's count plus the index in name order; arguments in parameter order.
        sb.AppendLine( $"{indent}    {layer} void __NetDispatchRpcLayer( int rpcId, ref global::ScriptCore.NetReader reader )" );
        sb.AppendLine( $"{indent}    {{" );
        if( derived )
        {
            sb.AppendLine( $"{indent}        if( rpcId < base.__NetRpcCount )" );
            sb.AppendLine( $"{indent}        {{" );
            sb.AppendLine( $"{indent}            base.__NetDispatchRpcLayer( rpcId, ref reader );" );
            sb.AppendLine( $"{indent}            return;" );
            sb.AppendLine( $"{indent}        }}" );
        }
        if( model.Rpcs.Count > 0 )
        {
            sb.AppendLine( derived ? $"{indent}        switch( rpcId - base.__NetRpcCount )" : $"{indent}        switch( rpcId )" );
            sb.AppendLine( $"{indent}        {{" );
            for( int id = 0; id < model.Rpcs.Count; ++id )
            {
                var rpc = model.Rpcs[id];
                sb.AppendLine( $"{indent}            case {id}:" );
                sb.AppendLine( $"{indent}            {{" );
                // Only on the side it's addressed to (offline: both): a client can't make the
                // server run a ClientRpc, nor a server a ServerRpc on a client.
                sb.AppendLine( rpc.Server
                    ? $"{indent}                if( global::ScriptCore.Net.IsClient ) {{ return; }}"
                    : $"{indent}                if( global::ScriptCore.Net.IsServer ) {{ return; }}" );
                for( int i = 0; i < rpc.Parameters.Count; ++i )
                {
                    var parameter = rpc.Parameters[i];
                    sb.AppendLine( $"{indent}                var a{i} = {ReadCall( "reader", parameter.Kind, parameter.TypeRef )};" );
                }
                if( rpc.Parameters.Count > 0 )
                {
                    sb.AppendLine( $"{indent}                if( reader.Overflowed )" );
                    sb.AppendLine( $"{indent}                {{" );
                    sb.AppendLine( $"{indent}                    return;" );
                    sb.AppendLine( $"{indent}                }}" );
                }
                var arguments = string.Join( ", ", Enumerable.Range( 0, rpc.Parameters.Count ).Select( i => "a" + i ) );
                sb.AppendLine( $"{indent}                this.On{rpc.Name}({( arguments.Length > 0 ? " " + arguments + " " : "" )});" );
                sb.AppendLine( $"{indent}                return;" );
                sb.AppendLine( $"{indent}            }}" );
            }
            sb.AppendLine( $"{indent}        }}" );
        }
        sb.AppendLine( $"{indent}    }}" );

        // The interface, on the root net script only: sizes come from the most derived layer.
        if( !derived )
        {
            sb.AppendLine();
            sb.AppendLine( $"{indent}    int global::ScriptCore.INetScript.NetFlags => this.__NetFlags;" );
            sb.AppendLine();
            sb.AppendLine( $"{indent}    int global::ScriptCore.INetScript.NetSavePredicted( global::System.Span<byte> destination )" );
            sb.AppendLine( $"{indent}    {{" );
            sb.AppendLine( $"{indent}        int size = this.__NetPredictedSize;" );
            sb.AppendLine( $"{indent}        if( size > 0 && destination.Length >= size )" );
            sb.AppendLine( $"{indent}        {{" );
            sb.AppendLine( $"{indent}            var writer = new global::ScriptCore.NetWriter( destination );" );
            sb.AppendLine( $"{indent}            this.__NetSavePredictedLayer( ref writer );" );
            sb.AppendLine( $"{indent}        }}" );
            sb.AppendLine( $"{indent}        return size;" );
            sb.AppendLine( $"{indent}    }}" );
            sb.AppendLine();
            sb.AppendLine( $"{indent}    void global::ScriptCore.INetScript.NetRestorePredicted( global::System.ReadOnlySpan<byte> source )" );
            sb.AppendLine( $"{indent}    {{" );
            sb.AppendLine( $"{indent}        int size = this.__NetPredictedSize;" );
            sb.AppendLine( $"{indent}        if( size > 0 && source.Length >= size )" );
            sb.AppendLine( $"{indent}        {{" );
            sb.AppendLine( $"{indent}            var reader = new global::ScriptCore.NetReader( source );" );
            sb.AppendLine( $"{indent}            this.__NetRestorePredictedLayer( ref reader );" );
            sb.AppendLine( $"{indent}        }}" );
            sb.AppendLine( $"{indent}    }}" );
            sb.AppendLine();
            sb.AppendLine( $"{indent}    int global::ScriptCore.INetScript.NetWriteReplicated( global::System.Span<byte> destination )" );
            sb.AppendLine( $"{indent}    {{" );
            sb.AppendLine( $"{indent}        int size = this.__NetReplicatedSize;" );
            sb.AppendLine( $"{indent}        if( size > 0 && destination.Length >= size )" );
            sb.AppendLine( $"{indent}        {{" );
            sb.AppendLine( $"{indent}            var writer = new global::ScriptCore.NetWriter( destination );" );
            sb.AppendLine( $"{indent}            this.__NetWriteReplicatedLayer( ref writer );" );
            sb.AppendLine( $"{indent}        }}" );
            sb.AppendLine( $"{indent}        return size;" );
            sb.AppendLine( $"{indent}    }}" );
            sb.AppendLine();
            sb.AppendLine( $"{indent}    void global::ScriptCore.INetScript.NetReadReplicated( global::System.ReadOnlySpan<byte> source, bool skipPredicted )" );
            sb.AppendLine( $"{indent}    {{" );
            sb.AppendLine( $"{indent}        int size = this.__NetReplicatedSize;" );
            sb.AppendLine( $"{indent}        if( size > 0 && source.Length >= size )" );
            sb.AppendLine( $"{indent}        {{" );
            sb.AppendLine( $"{indent}            var reader = new global::ScriptCore.NetReader( source );" );
            sb.AppendLine( $"{indent}            this.__NetReadReplicatedLayer( ref reader, skipPredicted );" );
            sb.AppendLine( $"{indent}        }}" );
            sb.AppendLine( $"{indent}    }}" );
            sb.AppendLine();
            sb.AppendLine( $"{indent}    void global::ScriptCore.INetScript.NetDispatchRpc( int rpcId, global::System.ReadOnlySpan<byte> args )" );
            sb.AppendLine( $"{indent}    {{" );
            sb.AppendLine( $"{indent}        var reader = new global::ScriptCore.NetReader( args );" );
            sb.AppendLine( $"{indent}        this.__NetDispatchRpcLayer( rpcId, ref reader );" );
            sb.AppendLine( $"{indent}    }}" );
        }

        // The send stubs: the bodies of the user's partial declarations.
        for( int id = 0; id < model.Rpcs.Count; ++id )
        {
            var rpc = model.Rpcs[id];
            int kind = rpc.Server ? 0 : rpc.Owner ? 1 : 2;
            var rpcIdExpr = derived ? $"base.__NetRpcCount + {id}" : id.ToString();
            var parameters = string.Join( ", ", rpc.Parameters.Select( p => p.TypeRef + " " + p.Name ) );
            var modifiers = rpc.Modifiers.Length > 0 ? rpc.Modifiers + " " : "";
            sb.AppendLine();
            sb.AppendLine( $"{indent}    {modifiers}partial void {EscapeIdentifier( rpc.Name )}({( parameters.Length > 0 ? " " + parameters + " " : "" )})" );
            sb.AppendLine( $"{indent}    {{" );
            if( rpc.Parameters.Count == 0 )
            {
                sb.AppendLine( $"{indent}        global::ScriptCore.Net.SendRpc( this.Entity, {rpcIdExpr}, {kind}, default, this );" );
            }
            else
            {
                bool hasStrings = rpc.Parameters.Any( p => p.Kind == NetKind.String );
                int size = rpc.Parameters.Sum( p => SizeOf( p.Kind ) );
                if( hasStrings )
                {
                    size = StringRpcBufferSize;
                }
                sb.AppendLine( $"{indent}        global::System.Span<byte> __buffer = stackalloc byte[{size}];" );
                sb.AppendLine( $"{indent}        var __writer = new global::ScriptCore.NetWriter( __buffer );" );
                foreach( var parameter in rpc.Parameters )
                {
                    sb.AppendLine( $"{indent}        {WriteCall( "__writer", parameter.Kind, parameter.Name )}" );
                }
                if( hasStrings )
                {
                    sb.AppendLine( $"{indent}        if( __writer.Overflowed )" );
                    sb.AppendLine( $"{indent}        {{" );
                    sb.AppendLine( $"{indent}            global::ScriptCore.Net.WarnRpcTooLarge( \"{model.TypeName}.{rpc.Name}\" );" );
                    sb.AppendLine( $"{indent}            return;" );
                    sb.AppendLine( $"{indent}        }}" );
                }
                sb.AppendLine( $"{indent}        global::ScriptCore.Net.SendRpc( this.Entity, {rpcIdExpr}, {kind}, __writer.Written, this );" );
            }
            sb.AppendLine( $"{indent}    }}" );
        }

        sb.AppendLine( $"{indent}}}" );
        if( model.Namespace is not null )
        {
            sb.AppendLine( "}" );
        }

        spc.AddSource( model.FullName + ".Net.g.cs", SourceText.From( sb.ToString(), Encoding.UTF8 ) );
    }
}
