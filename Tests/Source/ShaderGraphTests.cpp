#include <doctest/doctest.h>
#include "ShaderGraph/Compiler.h"
#include "ShaderGraph/Graph.h"
#include "ShaderGraph/NodeDef.h"
#include "ShaderGraph/Targets.h"
#include "Graphics/ShaderCompiler.h"
#include "Path.h"
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace ShaderGraph;

namespace
{
    const Node& OutputOf( const Graph& InGraph )
    {
        return *InGraph.FindOutputNode();
    }

    bool HasError( const CompileResult& InResult, uint32_t InNode, const std::string& InFragment )
    {
        for( const Diagnostic& diagnostic : InResult.Diagnostics )
        {
            if( diagnostic.Level == Diagnostic::Severity::Error && ( InNode == 0 || diagnostic.NodeId == InNode ) && diagnostic.Message.find( InFragment ) != std::string::npos )
            {
                return true;
            }
        }
        return false;
    }

    std::string FileText( const CompileResult& InResult, const std::string& InName )
    {
        const GeneratedFile* file = InResult.FindFile( InName );
        return file ? file->Source : std::string();
    }

    size_t Count( const std::string& InText, const std::string& InNeedle )
    {
        size_t count = 0;
        for( size_t at = InText.find( InNeedle ); at != std::string::npos; at = InText.find( InNeedle, at + 1 ) )
        {
            ++count;
        }
        return count;
    }

    // Compiles every generated vertex/fragment file with shaderc for each portable profile.
    // Returns the failures (with the compiler's log).
    std::string CompileWithShaderc( const CompileResult& InResult, const std::string& InName )
    {
        const std::filesystem::path directory = std::filesystem::path( Path( ".tmp/ShaderGraphTests" ).FullPath ) / InName;
        std::filesystem::remove_all( directory );
        std::string error;
        if( !WriteGeneratedFiles( InResult, directory.string(), &error ) )
        {
            return error;
        }
        std::string failures;
        for( const GeneratedFile& file : InResult.Files )
        {
            if( file.Kind == "varyings" )
            {
                continue;
            }
            for( const Moonlight::ShaderCompiler::Profile& profile : Moonlight::ShaderCompiler::GetPortableProfiles() )
            {
                Moonlight::ShaderCompiler::Request request;
                request.SourcePath = ( directory / file.Name ).string();
                request.OutputPath = request.SourcePath + "." + profile.Name + ".bin";
                request.ShaderStage = file.Kind == "vertex" ? Moonlight::ShaderCompiler::Stage::Vertex : Moonlight::ShaderCompiler::Stage::Fragment;
                request.TargetProfile = profile;
                // Fragment shaders pair with their vertex shader's varyings (Shadow.frag has its own).
                const Moonlight::ShaderCompiler::Result result = Moonlight::ShaderCompiler::Compile( request );
                if( !result.Success )
                {
                    failures += InName + "/" + file.Name + " [" + profile.Name + "]\n" + result.Log + "\n";
                }
            }
        }
        return failures;
    }

    // A graph with one instance of every node the target offers (in InStage), each output folded
    // into the target's colour (fragment) or vertex offset (vertex) through a chain of Adds.
    Graph BuildEveryNodeGraph( Target InTarget, Stage InStage )
    {
        Graph graph = Graph::CreateDefault( InTarget );
        graph.Settings.AlphaClip = true;
        std::vector<std::string> propertyIds;
        propertyIds.push_back( graph.AddProperty( "Float Property", ValueType::Float ).Id );
        propertyIds.push_back( graph.AddProperty( "Color Property", ValueType::Vec4 ).Id );
        propertyIds.push_back( graph.AddProperty( "Texture Property", ValueType::Texture2D ).Id );
        propertyIds.push_back( graph.AddProperty( "Bool Property", ValueType::Bool ).Id );

        const uint32_t outputNode = OutputOf( graph ).Id;
        const std::string sink = InStage == Stage::Vertex ? "Position Offset" : ( InTarget == Target::Unlit ? "Color" : "Base Color" );
        uint32_t accumulator = 0;
        std::string accumulatorPin;
        auto fold = [&]( uint32_t InNode, const std::string& InPin ) {
            if( accumulator == 0 )
            {
                accumulator = InNode;
                accumulatorPin = InPin;
                return;
            }
            const uint32_t add = graph.AddNode( "Add" ).Id;
            graph.Connect( accumulator, accumulatorPin, add, "A" );
            graph.Connect( InNode, InPin, add, "B" );
            accumulator = add;
            accumulatorPin = "Out";
        };

        std::vector<const NodeDef*> defs = NodeRegistry::Get().GetAll();
        std::vector<std::pair<const NodeDef*, std::string>> instances;
        for( const NodeDef* def : defs )
        {
            if( def->Type == "Output" || !def->SupportsTarget( InTarget ) )
            {
                continue;
            }
            if( def->Type == "Property" )
            {
                for( const std::string& id : propertyIds )
                {
                    instances.push_back( { def, id } );
                }
                continue;
            }
            instances.push_back( { def, "" } );
        }

        for( const auto& instance : instances )
        {
            const NodeDef* def = instance.first;
            const uint32_t id = graph.AddNode( def->Type ).Id;
            if( !instance.second.empty() )
            {
                graph.FindNode( id )->Settings["Property"] = instance.second;
            }
            if( !( def->GetStages( graph, *graph.FindNode( id ) ) & StageBit( InStage ) ) )
            {
                graph.RemoveNode( id );
                continue;
            }
            std::vector<PinDef> inputs;
            std::vector<PinDef> outputs;
            def->BuildPins( graph, *graph.FindNode( id ), inputs, outputs );
            for( const PinDef& output : outputs )
            {
                if( IsVectorType( output.Type ) )
                {
                    fold( id, output.Name );
                }
                else if( output.Type == ValueType::Bool )
                {
                    const uint32_t branch = graph.AddNode( "Branch" ).Id;
                    graph.Connect( id, output.Name, branch, "Predicate" );
                    fold( branch, "Out" );
                }
                else if( output.Type == ValueType::Texture2D )
                {
                    const uint32_t sample = graph.AddNode( "SampleTexture2DLOD" ).Id;
                    graph.Connect( id, output.Name, sample, "Texture" );
                    fold( sample, "RGB" );
                }
                else if( output.Type == ValueType::TextureCube )
                {
                    const uint32_t sample = graph.AddNode( "SampleCubemap" ).Id;
                    graph.Connect( id, output.Name, sample, "Cubemap" );
                    fold( sample, "Out" );
                }
            }
        }
        graph.Connect( accumulator, accumulatorPin, outputNode, sink );
        return graph;
    }
}

TEST_CASE( "ShaderGraph: value conversions are valid in every dialect" )
{
    CHECK( ConvertExpression( "x", ValueType::Float, ValueType::Vec3 ) == "vec3_splat(x)" );
    CHECK( ConvertExpression( "v", ValueType::Vec4, ValueType::Vec2 ) == "(v).xy" );
    CHECK( ConvertExpression( "v", ValueType::Vec2, ValueType::Vec4 ) == "vec4(v, 0.0, 1.0)" );
    CHECK( ConvertExpression( "v", ValueType::Vec3, ValueType::Float ) == "(v).x" );
    CHECK( ConvertExpression( "b", ValueType::Bool, ValueType::Float ) == "(b ? 1.0 : 0.0)" );
    CHECK_FALSE( CanConvert( ValueType::Texture2D, ValueType::Vec3 ) );
    CHECK( FloatLiteral( 1.f ) == "1.0" );
    CHECK( FloatLiteral( -0.f ) == "0.0" );
    CHECK( FloatLiteral( 0.25f ) == "0.25" );
    CHECK( FloatLiteral( 1e20f ).find( 'e' ) != std::string::npos );
    CHECK( VectorLiteral( Vector4( 1.f, 2.f, 3.f, 4.f ), ValueType::Vec3 ) == "vec3(1.0, 2.0, 3.0)" );
    CHECK( SanitizeIdentifier( "My Tint__2" ) == "MyTint_2" );
    CHECK( SanitizeIdentifier( "2Fast" ) == "v2Fast" );
}

TEST_CASE( "ShaderGraph: a default Lit graph compiles to the mesh variants" )
{
    Graph graph = Graph::CreateDefault( Target::Lit );
    CompileResult result = Compile( graph, { "Assets/Test.shadergraph" } );
    DOCTEST_INFO( result.FormatDiagnostics( graph ) );
    REQUIRE( result.Success );
    CHECK( result.FindFile( "Forward.vert" ) );
    CHECK( result.FindFile( "Skinned.vert" ) );
    CHECK( result.FindFile( "Forward.frag" ) );
    CHECK( result.FindFile( "Forward.var" ) );
    CHECK_FALSE( result.FindFile( "Shadow.frag" ) );
    CHECK( result.Manifest["Target"] == "Lit" );
    CHECK( result.Manifest["Programs"].contains( "Skinned" ) );
    CHECK_FALSE( result.Manifest["Programs"].contains( "Shadow" ) );
    const std::string fragment = FileText( result, "Forward.frag" );
    CHECK( fragment.find( "vec3 sgOut_BaseColor = vec3(0.5, 0.5, 0.5);" ) != std::string::npos );
    CHECK( fragment.find( "shadeStandardSurface" ) != std::string::npos );
    CHECK( FileText( result, "Skinned.vert" ).find( "a_indices, a_weight" ) != std::string::npos );
    CHECK( FileText( result, "Forward.vert" ).find( "#define SG_SKINNED" ) == std::string::npos );
}

TEST_CASE( "ShaderGraph: dynamic vectors widen to the widest input" )
{
    Graph graph = Graph::CreateDefault( Target::Unlit );
    const uint32_t color = graph.AddNode( "Vector3" ).Id;
    graph.FindNode( color )->Settings["Value"] = json::array( { 1.f, 0.5f, 0.25f, 0.f } );
    const uint32_t scalar = graph.AddNode( "Float" ).Id;
    graph.FindNode( scalar )->Settings["Value"] = 2.f;
    const uint32_t add = graph.AddNode( "Add" ).Id;
    graph.Connect( color, "Out", add, "A" );
    graph.Connect( scalar, "Out", add, "B" );
    graph.Connect( add, "Out", OutputOf( graph ).Id, "Color" );

    CompileResult result = Compile( graph );
    DOCTEST_INFO( result.FormatDiagnostics( graph ) );
    REQUIRE( result.Success );
    CHECK( result.PinTypes[{ add, "out:Out" }] == ValueType::Vec3 );
    CHECK( result.PinTypes[{ add, "in:B" }] == ValueType::Vec3 );
    const std::string fragment = FileText( result, "Forward.frag" );
    CHECK( fragment.find( "vec3 n" + std::to_string( add ) + "_Out = vec3(1.0, 0.5, 0.25) + vec3_splat(2.0);" ) != std::string::npos );
}

TEST_CASE( "ShaderGraph: loops, bad links and stage misuse are reported on their nodes" )
{
    SUBCASE( "loop" )
    {
        Graph graph = Graph::CreateDefault( Target::Unlit );
        const uint32_t aId = graph.AddNode( "Add" ).Id;
        const uint32_t bId = graph.AddNode( "Add" ).Id;
        graph.Connect( aId, "Out", bId, "A" );
        graph.Connect( bId, "Out", aId, "A" );
        graph.Connect( bId, "Out", OutputOf( graph ).Id, "Color" );
        CompileResult result = Compile( graph );
        CHECK_FALSE( result.Success );
        CHECK( HasError( result, 0, "loop" ) );
    }
    SUBCASE( "type mismatch" )
    {
        Graph graph = Graph::CreateDefault( Target::Unlit );
        const uint32_t textureId = graph.AddNode( "Texture2DAsset" ).Id;
        graph.Connect( textureId, "Out", OutputOf( graph ).Id, "Color" );
        CompileResult result = Compile( graph );
        CHECK_FALSE( result.Success );
        CHECK( HasError( result, OutputOf( graph ).Id, "Can't connect" ) );
    }
    SUBCASE( "fragment-only node feeding the vertex stage" )
    {
        Graph graph = Graph::CreateDefault( Target::Lit );
        const uint32_t ddxId = graph.AddNode( "DDX" ).Id;
        graph.Connect( ddxId, "Out", OutputOf( graph ).Id, "Position Offset" );
        CompileResult result = Compile( graph );
        CHECK_FALSE( result.Success );
        CHECK( HasError( result, ddxId, "vertex stage" ) );
    }
    SUBCASE( "builtin missing from a stage" )
    {
        Graph graph = Graph::CreateDefault( Target::Lit );
        const uint32_t screenId = graph.AddNode( "ScreenPosition" ).Id;
        graph.Connect( screenId, "UV", OutputOf( graph ).Id, "Position Offset" );
        CompileResult result = Compile( graph );
        CHECK_FALSE( result.Success );
        CHECK( HasError( result, screenId, "vertex" ) );
    }
    SUBCASE( "deleted property" )
    {
        Graph graph = Graph::CreateDefault( Target::Lit );
        Node& property = graph.AddNode( "Property" );
        property.Settings["Property"] = "p404";
        const uint32_t propertyId = property.Id;
        graph.Connect( propertyId, "Out", OutputOf( graph ).Id, "Metallic" );
        CompileResult result = Compile( graph );
        CHECK( HasError( result, propertyId, "deleted" ) );
    }
}

TEST_CASE( "ShaderGraph: identical nodes are emitted once" )
{
    Graph graph = Graph::CreateDefault( Target::Unlit );
    const uint32_t firstId = graph.AddNode( "GradientNoise" ).Id;
    const uint32_t secondId = graph.AddNode( "GradientNoise" ).Id;
    const uint32_t addId = graph.AddNode( "Add" ).Id;
    graph.Connect( firstId, "Out", addId, "A" );
    graph.Connect( secondId, "Out", addId, "B" );
    graph.Connect( addId, "Out", OutputOf( graph ).Id, "Color" );
    CompileResult result = Compile( graph );
    REQUIRE( result.Success );
    const std::string fragment = FileText( result, "Forward.frag" );
    CHECK( Count( fragment, "sgGradientNoise(" ) == 1 );

}

TEST_CASE( "ShaderGraph: properties pack into the parameter block and samplers" )
{
    Graph graph = Graph::CreateDefault( Target::Lit );
    const std::string tintId = graph.AddProperty( "Tint", ValueType::Vec3 ).Id;
    graph.FindProperty( tintId )->Default = Vector4( 1.f, 0.f, 0.f, 0.f );
    graph.FindProperty( tintId )->IsColor = true;
    const std::string albedoId = graph.AddProperty( "Albedo", ValueType::Texture2D ).Id;
    graph.FindProperty( albedoId )->Texture = "Assets/Textures/Test.png";
    const std::string roughId = graph.AddProperty( "Roughness", ValueType::Float ).Id;
    graph.FindProperty( roughId )->Default = Vector4( 0.25f );
    graph.FindProperty( roughId )->Min = 0.f;
    graph.FindProperty( roughId )->Max = 1.f;

    auto propertyNode = [&graph]( const std::string& InId ) {
        const uint32_t id = graph.AddNode( "Property" ).Id;
        graph.FindNode( id )->Settings["Property"] = InId;
        return id;
    };
    const uint32_t tintNodeId = propertyNode( tintId );
    const uint32_t albedoNodeId = propertyNode( albedoId );
    const uint32_t sampleId = graph.AddNode( "SampleTexture2D" ).Id;
    const uint32_t multiplyId = graph.AddNode( "Multiply" ).Id;
    const uint32_t roughNodeId = propertyNode( roughId );
    const uint32_t output = OutputOf( graph ).Id;
    graph.Connect( albedoNodeId, "Out", sampleId, "Texture" );
    graph.Connect( sampleId, "RGB", multiplyId, "A" );
    graph.Connect( tintNodeId, "Out", multiplyId, "B" );
    graph.Connect( multiplyId, "Out", output, "Base Color" );
    graph.Connect( roughNodeId, "Out", output, "Roughness" );

    CompileResult result = Compile( graph );
    DOCTEST_INFO( result.FormatDiagnostics( graph ) );
    REQUIRE( result.Success );
    REQUIRE( result.Parameters.size() == 2 );
    CHECK( result.Parameters[0].Slot == 0 );
    CHECK( result.Parameters[1].Slot == 1 );
    REQUIRE( result.Textures.size() == 1 );
    CHECK( result.Textures[0].Asset == "Assets/Textures/Test.png" );
    const std::string fragment = FileText( result, "Forward.frag" );
    CHECK( fragment.find( "SAMPLER2D(s_sgTex0, 0);" ) != std::string::npos );
    CHECK( fragment.find( "u_sgParams[0].xyz" ) != std::string::npos );
    CHECK( fragment.find( "u_sgParams[1].x" ) != std::string::npos );
    CHECK( result.Manifest["Parameters"][1]["Range"][1] == 1.f );
    CHECK( result.Manifest["Textures"][0]["Exposed"] == true );
}

TEST_CASE( "ShaderGraph: vertex offset and alpha clip add a shadow caster" )
{
    Graph graph = Graph::CreateDefault( Target::Lit );
    const uint32_t waveId = graph.AddNode( "Vector3" ).Id;
    graph.FindNode( waveId )->Settings["Value"] = json::array( { 0.f, 1.f, 0.f } );
    graph.Connect( waveId, "Out", OutputOf( graph ).Id, "Position Offset" );
    CompileResult result = Compile( graph );
    REQUIRE( result.Success );
    CHECK( result.FindFile( "Shadow.frag" ) );
    CHECK( result.Manifest["Programs"]["Shadow"][1] == "Shadow" );

    graph.Settings.CastShadows = false;
    CHECK_FALSE( Compile( graph ).FindFile( "Shadow.frag" ) );
}

TEST_CASE( "ShaderGraph: graphs round-trip through JSON" )
{
    Graph graph = Graph::CreateDefault( Target::Unlit );
    graph.Settings.Surface = SurfaceType::Transparent;
    graph.Settings.Blend = BlendMode::Additive;
    graph.AddProperty( "Speed", ValueType::Float ).Default = Vector4( 3.f );
    const uint32_t pannerId = graph.AddNode( "Panner", Vector2( 10.f, 20.f ) ).Id;
    graph.FindNode( pannerId )->SetDefault( "Speed", Vector4( 1.f, 2.f, 0.f, 0.f ) );
    graph.Connect( pannerId, "Out", OutputOf( graph ).Id, "Color" );

    json saved;
    graph.Serialize( saved );
    Graph loaded;
    REQUIRE( loaded.Deserialize( saved ) );
    CHECK( loaded.Settings.GraphTarget == Target::Unlit );
    CHECK( loaded.Settings.Blend == BlendMode::Additive );
    REQUIRE( loaded.Properties.size() == 1 );
    CHECK( loaded.Properties[0].Default.x == 3.f );
    REQUIRE( loaded.FindNode( pannerId ) );
    CHECK( loaded.FindNode( pannerId )->FindDefault( "Speed" )->y == 2.f );
    CHECK( loaded.Links.size() == 1 );
    // New ids continue after the loaded ones.
    CHECK( loaded.AddNode( "Float" ).Id > pannerId );
}

TEST_CASE( "ShaderGraph: shaderc errors map back to nodes" )
{
    const std::vector<Diagnostic> diagnostics = MapShaderErrors( "Forward.frag(42): error: 'n17_Out' : undeclared identifier\nsome noise\n" );
    REQUIRE( diagnostics.size() == 1 );
    CHECK( diagnostics[0].NodeId == 17 );
}

TEST_CASE( "ShaderGraph: every node compiles with shaderc in every target" * doctest::timeout( 600 ) )
{
    for( const Target target : { Target::Lit, Target::Unlit } )
    {
        for( const Stage stage : { Stage::Fragment, Stage::Vertex } )
        {
            const Graph graph = BuildEveryNodeGraph( target, stage );
            const std::string name = std::string( GetTargetName( target ) ) + "_" + GetStageName( stage );
            const CompileResult result = Compile( graph, { "Tests/" + name + ".shadergraph" } );
            DOCTEST_INFO( name );
            DOCTEST_INFO( result.FormatDiagnostics( graph ) );
            REQUIRE( result.Success );
            const std::string failures = CompileWithShaderc( result, name );
            DOCTEST_INFO( failures );
            CHECK( failures.empty() );
            if( !failures.empty() )
            {
                for( const Diagnostic& diagnostic : MapShaderErrors( failures ) )
                {
                    const Node* node = graph.FindNode( diagnostic.NodeId );
                    const std::string text = ( node ? node->Type : std::string( "?" ) ) + ": " + diagnostic.Message;
                    DOCTEST_MESSAGE( text );
                }
            }
        }
    }
}

// The project's graphs and their committed generated sources (<Graph>.sg/) must agree: a graph
// edited without recompiling, or a compiler change that alters output, fails here. Run with
// ME_SHADERGRAPH_UPDATE=1 to rewrite them.
TEST_CASE( "ShaderGraph: project graphs match their generated sources" )
{
    const bool update = std::getenv( "ME_SHADERGRAPH_UPDATE" ) != nullptr;
    const std::filesystem::path root = Path( "Assets" ).FullPath;
    if( !std::filesystem::exists( root ) )
    {
        return;
    }
    for( const auto& entry : std::filesystem::recursive_directory_iterator( root ) )
    {
        if( entry.path().extension() != Graph::kExtension )
        {
            continue;
        }
        const std::string fullPath = entry.path().string();
        Graph graph;
        std::string error;
        DOCTEST_INFO( fullPath );
        REQUIRE_MESSAGE( graph.Load( fullPath, &error ), error );
        if( update )
        {
            const CompileResult result = CompileAndExport( fullPath, graph );
            DOCTEST_INFO( result.FormatDiagnostics( graph ) );
            CHECK( result.Success );
            continue;
        }
        CompileOptions options;
        options.GraphPath = Path( fullPath ).GetLocalPathString();
        const CompileResult result = Compile( graph, options );
        DOCTEST_INFO( result.FormatDiagnostics( graph ) );
        CHECK( result.Success );
        const std::string directory = GetGeneratedDirectory( fullPath );
        for( const GeneratedFile& file : result.Files )
        {
            std::ifstream stream( directory + "/" + file.Name, std::ios::binary );
            std::stringstream contents;
            contents << stream.rdbuf();
            DOCTEST_INFO( file.Name );
            CHECK_MESSAGE( contents.str() == file.Source, "out of date: recompile the graph (or run the tests with ME_SHADERGRAPH_UPDATE=1)" );
        }
    }
}
