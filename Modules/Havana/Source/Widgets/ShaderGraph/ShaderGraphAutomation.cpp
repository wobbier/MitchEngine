#include "ShaderGraphAutomation.h"

#if USING( ME_EDITOR )
#include <filesystem>
#include <map>
#include "Path.h"
#include "ShaderGraph/NodeDef.h"
#include "ShaderGraphDocument.h"
#include "ShaderGraphWidget.h"

using namespace ShaderGraph;

namespace
{
    std::map<std::string, uint32_t> s_aliases;
    uint32_t s_compileMark = 0;


    std::string Trim( const std::string& InText )
    {
        const size_t first = InText.find_first_not_of( " \t" );
        if( first == std::string::npos )
        {
            return {};
        }
        return InText.substr( first, InText.find_last_not_of( " \t" ) - first + 1 );
    }


    std::vector<std::string> Split( const std::string& InText, char InSeparator = '|' )
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while( true )
        {
            const size_t at = InText.find( InSeparator, start );
            parts.push_back( Trim( InText.substr( start, at == std::string::npos ? std::string::npos : at - start ) ) );
            if( at == std::string::npos )
            {
                return parts;
            }
            start = at + 1;
        }
    }


    // "alias:Pin Name" -> (node id, pin).
    bool ResolvePin( ShaderGraphDocument& InDocument, const std::string& InText, uint32_t& OutNode, std::string& OutPin, std::string& OutError )
    {
        const size_t colon = InText.find( ':' );
        if( colon == std::string::npos )
        {
            OutError = "expected alias:Pin, got '" + InText + "'";
            return false;
        }
        const std::string alias = Trim( InText.substr( 0, colon ) );
        OutPin = Trim( InText.substr( colon + 1 ) );
        if( alias == "out" )
        {
            const Node* output = InDocument.GetGraph().FindOutputNode();
            OutNode = output ? output->Id : 0;
            return OutNode != 0;
        }
        auto found = s_aliases.find( alias );
        if( found == s_aliases.end() )
        {
            OutError = "unknown alias '" + alias + "'";
            return false;
        }
        OutNode = found->second;
        return true;
    }


    bool ResolveNode( const std::string& InAlias, uint32_t& OutNode, std::string& OutError )
    {
        auto found = s_aliases.find( InAlias );
        if( found == s_aliases.end() )
        {
            OutError = "unknown alias '" + InAlias + "'";
            return false;
        }
        OutNode = found->second;
        return true;
    }


    bool ParseBool( const std::string& InText )
    {
        return InText == "true" || InText == "1" || InText == "yes";
    }
}


void ShaderGraphAutomation::Reset()
{
    s_aliases.clear();
    s_compileMark = 0;
}


bool ShaderGraphAutomation::Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError )
{
    ShaderGraphWidget* widget = ShaderGraphWidget::Get();
    if( !widget )
    {
        OutError = "no shader graph widget";
        return false;
    }
    const std::vector<std::string> args = Split( InArgs );

    if( InCommand == "sg-new" )
    {
        if( args.size() != 2 )
        {
            OutError = "sg-new <Target> | <path>";
            return false;
        }
        Graph graph = Graph::CreateDefault( TargetFromName( args[0] == "Post Process" ? "PostProcess" : args[0] ) );
        std::error_code ignored;
        std::filesystem::create_directories( std::filesystem::path( Path( args[1] ).FullPath ).parent_path(), ignored );
        if( !graph.Save( Path( args[1] ).FullPath ) )
        {
            OutError = "can't write " + args[1];
            return false;
        }
        s_aliases.clear();
        return widget->Open( args[1] ) != nullptr || ( OutError = "can't open " + args[1], false );
    }
    if( InCommand == "sg-open" )
    {
        s_aliases.clear();
        ShaderGraphDocument* document = widget->Open( InArgs );
        if( !document )
        {
            OutError = "can't open " + InArgs;
            return false;
        }
        // Scripts can address existing nodes by "#<id>".
        for( const Node& node : document->GetGraph().Nodes )
        {
            s_aliases["#" + std::to_string( node.Id )] = node.Id;
        }
        return true;
    }
    if( InCommand == "sg-close" )
    {
        return widget->Close( InArgs ) || ( OutError = InArgs + " isn't open", false );
    }

    ShaderGraphDocument* document = widget->GetActive();
    if( !document )
    {
        OutError = "no shader graph is open";
        return false;
    }
    Graph& graph = document->GetGraph();

    if( InCommand == "sg-add" )
    {
        if( args.size() < 2 )
        {
            OutError = "sg-add <alias> | <NodeType> [| settings]";
            return false;
        }
        json settings = json::object();
        if( args.size() > 2 )
        {
            settings = json::parse( args[2], nullptr, false );
            if( settings.is_discarded() )
            {
                OutError = "bad settings json";
                return false;
            }
        }
        const float x = static_cast<float>( s_aliases.size() % 6 ) * 220.f - 900.f;
        const float y = static_cast<float>( s_aliases.size() / 6 ) * 180.f;
        const uint32_t id = document->AddNode( args[1], Vector2( x, y ), settings );
        if( !id )
        {
            OutError = "unknown node type " + args[1];
            return false;
        }
        s_aliases[args[0]] = id;
        return true;
    }
    if( InCommand == "sg-add-property" )
    {
        if( args.size() != 3 )
        {
            OutError = "sg-add-property <alias> | <Type> | <Name>";
            return false;
        }
        const bool color = args[1] == "Color";
        const std::string id = document->AddProperty( args[2], color ? ValueType::Vec4 : ValueTypeFromName( args[1] ) );
        if( color )
        {
            graph.FindProperty( id )->IsColor = true;
        }
        const uint32_t node = document->AddNode( "Property", Vector2( -1100.f, static_cast<float>( s_aliases.size() ) * 60.f ), json{ { "Property", id } } );
        s_aliases[args[0]] = node;
        return true;
    }
    if( InCommand == "sg-link" )
    {
        if( args.size() != 2 )
        {
            OutError = "sg-link <alias>:<Pin> | <alias>:<Pin>";
            return false;
        }
        uint32_t from = 0;
        uint32_t to = 0;
        std::string fromPin;
        std::string toPin;
        if( !ResolvePin( *document, args[0], from, fromPin, OutError ) || !ResolvePin( *document, args[1], to, toPin, OutError ) )
        {
            return false;
        }
        std::string reason;
        if( !document->Connect( from, fromPin, to, toPin, &reason ) )
        {
            OutError = "can't link: " + reason;
            return false;
        }
        return true;
    }
    if( InCommand == "sg-unlink" )
    {
        uint32_t node = 0;
        std::string pin;
        if( !ResolvePin( *document, InArgs, node, pin, OutError ) )
        {
            return false;
        }
        document->Disconnect( node, pin );
        return true;
    }
    if( InCommand == "sg-set" || InCommand == "sg-default" )
    {
        uint32_t node = 0;
        if( args.size() != 3 || !ResolveNode( args[0], node, OutError ) )
        {
            OutError = OutError.empty() ? InCommand + " <alias> | <name> | <json>" : OutError;
            return false;
        }
        const json value = json::parse( args[2], nullptr, false );
        if( value.is_discarded() )
        {
            OutError = "bad json value " + args[2];
            return false;
        }
        if( InCommand == "sg-set" )
        {
            document->SetNodeSetting( node, args[1], value );
            return true;
        }
        Vector4 vector;
        if( value.is_number() )
        {
            vector = Vector4( value.get<float>(), 0.f, 0.f, 0.f );
        }
        else if( value.is_array() )
        {
            float* components[4] = { &vector.x, &vector.y, &vector.z, &vector.w };
            for( size_t i = 0; i < value.size() && i < 4; ++i )
            {
                *components[i] = value[i].get<float>();
            }
        }
        document->SetInputDefault( node, args[1], vector );
        return true;
    }
    if( InCommand == "sg-target" )
    {
        document->SetTarget( TargetFromName( InArgs == "Post Process" ? "PostProcess" : InArgs ) );
        return true;
    }
    if( InCommand == "sg-delete" )
    {
        uint32_t node = 0;
        if( !ResolveNode( InArgs, node, OutError ) )
        {
            return false;
        }
        document->RemoveNodes( { node } );
        return true;
    }
    if( InCommand == "sg-frame" )
    {
        document->RequestFrameAll();
        return true;
    }
    if( InCommand == "sg-compile" )
    {
        document->CompileNow();
        return true;
    }
    if( InCommand == "sg-save" )
    {
        return document->Save() || ( OutError = "save failed", false );
    }
    if( InCommand == "sg-undo" )
    {
        document->GetUndo().Undo();
        return true;
    }
    if( InCommand == "sg-redo" )
    {
        document->GetUndo().Redo();
        return true;
    }
    if( InCommand == "sg-assert-compiled" )
    {
        document->CompileNow();
        if( !document->GetCompileResult().Success )
        {
            OutError = "graph doesn't compile:\n" + document->GetCompileResult().FormatDiagnostics( graph );
            return false;
        }
        return true;
    }
    if( InCommand == "sg-assert-error" )
    {
        uint32_t node = 0;
        if( !ResolveNode( InArgs, node, OutError ) )
        {
            return false;
        }
        document->CompileNow();
        for( const Diagnostic& diagnostic : document->GetCompileResult().Diagnostics )
        {
            if( diagnostic.NodeId == node && diagnostic.Level == Diagnostic::Severity::Error )
            {
                return true;
            }
        }
        OutError = "expected an error on " + InArgs;
        return false;
    }
    if( InCommand == "sg-assert-nodes" || InCommand == "sg-assert-links" )
    {
        const size_t expected = static_cast<size_t>( std::atoi( InArgs.c_str() ) );
        const size_t actual = InCommand == "sg-assert-nodes" ? graph.Nodes.size() : graph.Links.size();
        if( actual != expected )
        {
            OutError = InCommand + ": " + std::to_string( actual ) + " != " + InArgs;
            return false;
        }
        return true;
    }
    if( InCommand == "sg-assert-code" || InCommand == "sg-assert-no-code" )
    {
        if( args.size() != 2 )
        {
            OutError = InCommand + " <file> | <text>";
            return false;
        }
        document->CompileNow();
        const GeneratedFile* file = document->GetCompileResult().FindFile( args[0] );
        const bool found = file && file->Source.find( args[1] ) != std::string::npos;
        if( found != ( InCommand == "sg-assert-code" ) )
        {
            OutError = InCommand + ": '" + args[1] + "' in " + args[0];
            return false;
        }
        return true;
    }
    if( InCommand == "sg-assert-dirty" )
    {
        if( document->IsDirty() != ParseBool( InArgs ) )
        {
            OutError = std::string( "dirty is " ) + ( document->IsDirty() ? "true" : "false" );
            return false;
        }
        return true;
    }
    if( InCommand == "sg-mark-compiles" )
    {
        s_compileMark = document->GetCompileCount();
        return true;
    }
    if( InCommand == "sg-assert-compiles" )
    {
        const uint32_t count = document->GetCompileCount() - s_compileMark;
        if( count != static_cast<uint32_t>( std::atoi( InArgs.c_str() ) ) )
        {
            OutError = "compiles since mark: " + std::to_string( count ) + " != " + InArgs;
            return false;
        }
        return true;
    }
    OutError = "unknown command " + InCommand;
    return false;
}
#endif
