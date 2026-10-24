using System.Collections.Generic;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace MitchEngine.Generators;

internal enum FieldKind
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

internal sealed class FieldModel
{
    public string Name = "";
    public FieldKind Kind;
    // The C# type for casts in generated code: "float", "global::My.Enum", "global::Vector3".
    public string CastType = "";
}

internal sealed class TypeModel
{
    public string? Namespace;
    public string TypeName = "";
    // "My.Ns.Type" without global::, for the hash, the registry name and the hint name.
    public string FullName = "";
    public List<FieldModel> Fields = new();
    public List<Diagnostic> Diagnostics = new();
    // False when a MEGEN001/MEGEN003 error makes generation impossible.
    public bool Emit;
}

// Generates, for every [ScriptCore.Generation.GenerateSchema] partial class:
//   - a static TypeSchema (field names, kinds, boxed accessors) registered via module initializer,
//   - SaveFields/LoadFields JSON serializers matching ScriptSerializer's reflection shape,
//   - a stable FNV-1a schema hash (net handshakes later).
// All of it reflection-free, so shipped builds stay AOT-friendly.
[Generator( LanguageNames.CSharp )]
public sealed class SchemaGenerator : IIncrementalGenerator
{
    public const string AttributeMetadataName = "ScriptCore.Generation.GenerateSchemaAttribute";
    private const string HideInInspectorMetadataName = "ScriptCore.HideInInspectorAttribute";

    public void Initialize( IncrementalGeneratorInitializationContext context )
    {
        var types = context.SyntaxProvider.ForAttributeWithMetadataName(
            AttributeMetadataName,
            static ( node, _ ) => node is TypeDeclarationSyntax,
            static ( ctx, _ ) => BuildModel( ctx ) );
        context.RegisterSourceOutput( types, static ( spc, model ) => Emit( spc, model ) );
    }


    private static TypeModel BuildModel( GeneratorAttributeSyntaxContext ctx )
    {
        var symbol = (INamedTypeSymbol)ctx.TargetSymbol;
        var syntax = (TypeDeclarationSyntax)ctx.TargetNode;
        var location = syntax.Identifier.GetLocation();

        var model = new TypeModel
        {
            Namespace = symbol.ContainingNamespace is { IsGlobalNamespace: false } ns ? ns.ToDisplayString() : null,
            TypeName = symbol.Name,
        };
        model.FullName = model.Namespace is null ? model.TypeName : model.Namespace + "." + model.TypeName;

        if( symbol.TypeKind != TypeKind.Class || symbol.ContainingType is not null || symbol.Arity > 0 )
        {
            model.Diagnostics.Add( Diagnostic.Create( SchemaDiagnostics.UnsupportedType, location, model.FullName ) );
            return model;
        }
        if( !syntax.Modifiers.Any( SyntaxKind.PartialKeyword ) )
        {
            model.Diagnostics.Add( Diagnostic.Create( SchemaDiagnostics.NotPartial, location, model.FullName ) );
            return model;
        }

        model.Emit = true;
        foreach( var member in symbol.GetMembers() )
        {
            if( member is not IFieldSymbol field
                || field.IsStatic || field.IsConst || field.IsImplicitlyDeclared
                || field.DeclaredAccessibility != Accessibility.Public )
            {
                continue;
            }
            // Matches the reflection path: [HideInInspector] fields are not persisted either.
            if( field.GetAttributes().Any( a => a.AttributeClass?.ToDisplayString() == "ScriptCore.HideInInspectorAttribute" ) )
            {
                continue;
            }

            var fieldLocation = field.Locations.FirstOrDefault() ?? location;
            if( field.IsReadOnly )
            {
                model.Diagnostics.Add( Diagnostic.Create( SchemaDiagnostics.UnsupportedField, fieldLocation,
                    model.FullName, field.Name, "readonly fields cannot be restored" ) );
                continue;
            }
            if( !TryMapKind( field.Type, out var kind, out var castType ) )
            {
                model.Diagnostics.Add( Diagnostic.Create( SchemaDiagnostics.UnsupportedField, fieldLocation,
                    model.FullName, field.Name, $"fields of type '{field.Type.ToDisplayString()}' are not supported yet" ) );
                continue;
            }

            model.Fields.Add( new FieldModel { Name = field.Name, Kind = kind, CastType = castType } );
        }

        return model;
    }


    private static bool TryMapKind( ITypeSymbol type, out FieldKind kind, out string castType )
    {
        switch( type.SpecialType )
        {
            case SpecialType.System_Boolean: kind = FieldKind.Bool; castType = "bool"; return true;
            case SpecialType.System_Int32: kind = FieldKind.Int; castType = "int"; return true;
            case SpecialType.System_Single: kind = FieldKind.Float; castType = "float"; return true;
            case SpecialType.System_Double: kind = FieldKind.Double; castType = "double"; return true;
            case SpecialType.System_String: kind = FieldKind.String; castType = "string"; return true;
        }

        if( type.TypeKind == TypeKind.Enum )
        {
            if( type is INamedTypeSymbol named && named.EnumUnderlyingType?.SpecialType == SpecialType.System_Int32 )
            {
                kind = FieldKind.Enum;
                castType = type.ToDisplayString( SymbolDisplayFormat.FullyQualifiedFormat );
                return true;
            }
            kind = default;
            castType = "";
            return false;
        }

        var display = type.ToDisplayString( SymbolDisplayFormat.FullyQualifiedFormat );
        if( display == "global::Vector2" )
        {
            kind = FieldKind.Vector2;
            castType = display;
            return true;
        }
        if( display == "global::Vector3" )
        {
            kind = FieldKind.Vector3;
            castType = display;
            return true;
        }

        kind = default;
        castType = "";
        return false;
    }


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


    private static void Emit( SourceProductionContext spc, TypeModel model )
    {
        foreach( var diagnostic in model.Diagnostics )
        {
            spc.ReportDiagnostic( diagnostic );
        }
        if( !model.Emit )
        {
            return;
        }

        var hashInput = new StringBuilder( model.FullName );
        foreach( var field in model.Fields )
        {
            hashInput.Append( '|' ).Append( field.Name ).Append( ':' ).Append( field.Kind );
        }
        var hash = Fnv1a64( hashInput.ToString() );

        var typeRef = model.Namespace is null ? "global::" + model.TypeName : "global::" + model.FullName;
        var sb = new StringBuilder();
        sb.AppendLine( "// <auto-generated by MitchEngine.Generators (CS-9); do not edit.>" );
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

        sb.AppendLine( $"{indent}partial class {model.TypeName}" );
        sb.AppendLine( $"{indent}{{" );

        // The schema: names, kinds and boxed accessors, no reflection anywhere.
        sb.AppendLine( $"{indent}    /// <summary>Generated field schema (CS-9): names, kinds and boxed accessors.</summary>" );
        sb.AppendLine( $"{indent}    public static global::ScriptCore.Generation.TypeSchema Schema {{ get; }} = new global::ScriptCore.Generation.TypeSchema(" );
        sb.AppendLine( $"{indent}        \"{model.FullName}\"," );
        sb.AppendLine( $"{indent}        0x{hash:X16}UL," );
        sb.AppendLine( $"{indent}        new global::ScriptCore.Generation.FieldDescriptor[]" );
        sb.AppendLine( $"{indent}        {{" );
        foreach( var field in model.Fields )
        {
            var kindRef = "global::ScriptCore.Generation.FieldKind." + field.Kind;
            var displayType = field.CastType.StartsWith( "global::" ) ? field.CastType.Substring( "global::".Length ) : field.CastType;
            string setterValue = field.Kind switch
            {
                FieldKind.Enum => $"({field.CastType})(int)value!",
                FieldKind.String => $"(string?)value!",
                _ => $"({field.CastType})value!",
            };
            sb.AppendLine( $"{indent}            new global::ScriptCore.Generation.FieldDescriptor( \"{field.Name}\", {kindRef}, \"{displayType}\"," );
            sb.AppendLine( $"{indent}                static instance => (({typeRef})instance).{field.Name}," );
            sb.AppendLine( $"{indent}                static ( instance, value ) => (({typeRef})instance).{field.Name} = {setterValue} )," );
        }
        sb.AppendLine( $"{indent}        }} );" );
        sb.AppendLine();

        // SaveFields: the same JSON object shape ScriptSerializer produces via reflection.
        sb.AppendLine( $"{indent}    /// <summary>Writes the fields as one JSON object, matching ScriptSerializer's shape.</summary>" );
        sb.AppendLine( $"{indent}    public void SaveFields( global::System.Text.Json.Utf8JsonWriter writer )" );
        sb.AppendLine( $"{indent}    {{" );
        sb.AppendLine( $"{indent}        writer.WriteStartObject();" );
        foreach( var field in model.Fields )
        {
            switch( field.Kind )
            {
                case FieldKind.Bool:
                    sb.AppendLine( $"{indent}        writer.WriteBoolean( \"{field.Name}\", this.{field.Name} );" );
                    break;
                case FieldKind.Int:
                case FieldKind.Float:
                case FieldKind.Double:
                    sb.AppendLine( $"{indent}        writer.WriteNumber( \"{field.Name}\", this.{field.Name} );" );
                    break;
                case FieldKind.Enum:
                    sb.AppendLine( $"{indent}        writer.WriteNumber( \"{field.Name}\", (int)this.{field.Name} );" );
                    break;
                case FieldKind.String:
                    sb.AppendLine( $"{indent}        if( this.{field.Name} is null ) {{ writer.WriteNull( \"{field.Name}\" ); }} else {{ writer.WriteString( \"{field.Name}\", this.{field.Name} ); }}" );
                    break;
                case FieldKind.Vector2:
                case FieldKind.Vector3:
                    sb.AppendLine( $"{indent}        writer.WritePropertyName( \"{field.Name}\" );" );
                    sb.AppendLine( $"{indent}        writer.WriteStartObject();" );
                    sb.AppendLine( $"{indent}        writer.WriteNumber( \"x\", this.{field.Name}.x );" );
                    sb.AppendLine( $"{indent}        writer.WriteNumber( \"y\", this.{field.Name}.y );" );
                    if( field.Kind == FieldKind.Vector3 )
                    {
                        sb.AppendLine( $"{indent}        writer.WriteNumber( \"z\", this.{field.Name}.z );" );
                    }
                    sb.AppendLine( $"{indent}        writer.WriteEndObject();" );
                    break;
            }
        }
        sb.AppendLine( $"{indent}        writer.WriteEndObject();" );
        sb.AppendLine( $"{indent}    }}" );
        sb.AppendLine();

        // LoadFields: tolerant reads; unknown or mistyped entries are skipped.
        sb.AppendLine( $"{indent}    /// <summary>Reads fields from a JSON object written by SaveFields or ScriptSerializer.</summary>" );
        sb.AppendLine( $"{indent}    public void LoadFields( global::System.Text.Json.JsonElement element )" );
        sb.AppendLine( $"{indent}    {{" );
        sb.AppendLine( $"{indent}        if( element.ValueKind != global::System.Text.Json.JsonValueKind.Object )" );
        sb.AppendLine( $"{indent}        {{" );
        sb.AppendLine( $"{indent}            return;" );
        sb.AppendLine( $"{indent}        }}" );
        foreach( var field in model.Fields )
        {
            sb.AppendLine( $"{indent}        {{" );
            sb.AppendLine( $"{indent}            global::System.Text.Json.JsonElement value;" );
            switch( field.Kind )
            {
                case FieldKind.Bool:
                    sb.AppendLine( $"{indent}            if( element.TryGetProperty( \"{field.Name}\", out value ) && ( value.ValueKind == global::System.Text.Json.JsonValueKind.True || value.ValueKind == global::System.Text.Json.JsonValueKind.False ) )" );
                    sb.AppendLine( $"{indent}            {{" );
                    sb.AppendLine( $"{indent}                this.{field.Name} = value.GetBoolean();" );
                    sb.AppendLine( $"{indent}            }}" );
                    break;
                case FieldKind.Int:
                case FieldKind.Enum:
                    sb.AppendLine( $"{indent}            int parsed;" );
                    sb.AppendLine( $"{indent}            if( element.TryGetProperty( \"{field.Name}\", out value ) && value.ValueKind == global::System.Text.Json.JsonValueKind.Number && value.TryGetInt32( out parsed ) )" );
                    sb.AppendLine( $"{indent}            {{" );
                    sb.AppendLine( $"{indent}                this.{field.Name} = {( field.Kind == FieldKind.Enum ? $"({field.CastType})parsed" : "parsed" )};" );
                    sb.AppendLine( $"{indent}            }}" );
                    break;
                case FieldKind.Float:
                    sb.AppendLine( $"{indent}            float parsed;" );
                    sb.AppendLine( $"{indent}            if( element.TryGetProperty( \"{field.Name}\", out value ) && value.ValueKind == global::System.Text.Json.JsonValueKind.Number && value.TryGetSingle( out parsed ) )" );
                    sb.AppendLine( $"{indent}            {{" );
                    sb.AppendLine( $"{indent}                this.{field.Name} = parsed;" );
                    sb.AppendLine( $"{indent}            }}" );
                    break;
                case FieldKind.Double:
                    sb.AppendLine( $"{indent}            double parsed;" );
                    sb.AppendLine( $"{indent}            if( element.TryGetProperty( \"{field.Name}\", out value ) && value.ValueKind == global::System.Text.Json.JsonValueKind.Number && value.TryGetDouble( out parsed ) )" );
                    sb.AppendLine( $"{indent}            {{" );
                    sb.AppendLine( $"{indent}                this.{field.Name} = parsed;" );
                    sb.AppendLine( $"{indent}            }}" );
                    break;
                case FieldKind.String:
                    sb.AppendLine( $"{indent}            if( element.TryGetProperty( \"{field.Name}\", out value ) )" );
                    sb.AppendLine( $"{indent}            {{" );
                    sb.AppendLine( $"{indent}                if( value.ValueKind == global::System.Text.Json.JsonValueKind.String ) {{ this.{field.Name} = value.GetString()!; }}" );
                    sb.AppendLine( $"{indent}                else if( value.ValueKind == global::System.Text.Json.JsonValueKind.Null ) {{ this.{field.Name} = null!; }}" );
                    sb.AppendLine( $"{indent}            }}" );
                    break;
                case FieldKind.Vector2:
                case FieldKind.Vector3:
                    sb.AppendLine( $"{indent}            if( element.TryGetProperty( \"{field.Name}\", out value ) && value.ValueKind == global::System.Text.Json.JsonValueKind.Object )" );
                    sb.AppendLine( $"{indent}            {{" );
                    sb.AppendLine( $"{indent}                var current = this.{field.Name};" );
                    sb.AppendLine( $"{indent}                global::System.Text.Json.JsonElement axis;" );
                    sb.AppendLine( $"{indent}                float parsed;" );
                    var axes = field.Kind == FieldKind.Vector3 ? new[] { "x", "y", "z" } : new[] { "x", "y" };
                    foreach( var axis in axes )
                    {
                        sb.AppendLine( $"{indent}                if( value.TryGetProperty( \"{axis}\", out axis ) && axis.ValueKind == global::System.Text.Json.JsonValueKind.Number && axis.TryGetSingle( out parsed ) ) {{ current.{axis} = parsed; }}" );
                    }
                    sb.AppendLine( $"{indent}                this.{field.Name} = current;" );
                    sb.AppendLine( $"{indent}            }}" );
                    break;
            }
            sb.AppendLine( $"{indent}        }}" );
        }
        sb.AppendLine( $"{indent}    }}" );
        sb.AppendLine();

        // Registration without reflection: a module initializer fills the weak registry.
        sb.AppendLine( $"{indent}    [global::System.Runtime.CompilerServices.ModuleInitializer]" );
        sb.AppendLine( $"{indent}    internal static void __RegisterGeneratedSchema()" );
        sb.AppendLine( $"{indent}    {{" );
        sb.AppendLine( $"{indent}        global::ScriptCore.Generation.SchemaRegistry.Register( typeof( {typeRef} ), Schema );" );
        sb.AppendLine( $"{indent}    }}" );

        sb.AppendLine( $"{indent}}}" );
        if( model.Namespace is not null )
        {
            sb.AppendLine( "}" );
        }

        spc.AddSource( model.FullName + ".Schema.g.cs", SourceText.From( sb.ToString(), Encoding.UTF8 ) );
    }
}
