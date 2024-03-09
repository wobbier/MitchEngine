#include "BasicNodes.h"
#include "imgui.h"
#include "Utils\HavanaUtils.h"
#include "Events\HavanaEvents.h"
#include "Resource\ResourceCache.h"
#include "Utils\ImGuiUtils.h"
#include "Utils\PlatformUtils.h"
#include "Utils\ShaderEditorUtils.h"

LessThanNode::LessThanNode( int& inId )
    : Node( inId++, "Less Than" )
{
    Inputs.emplace_back( inId++, "A", PinType::Int );
    Inputs.emplace_back( inId++, "B", PinType::Int );
    Outputs.emplace_back( inId++, "Output", PinType::Bool );

    BuildNode();
}

bool LessThanNode::OnEvaluate()
{
    for( auto& input : Inputs )
    {
        if( input.Node == this )
        {
            continue;
        }

        input.Node->OnEvaluate();
    }

    // Is this a good idea?
    int value1 = Inputs[0].LinkedInput ? std::get<int>( Inputs[0].LinkedInput->Data ) : std::get<int>( Inputs[0].Data );
    int value2 = Inputs[1].LinkedInput ? std::get<int>( Inputs[1].LinkedInput->Data ) : std::get<int>( Inputs[1].Data );

    // loop over pins and check for new data?
    if( value1 < value2 )
    {
        Outputs[0].Name = "Output(A)";
        return true;
    }
    else
    {
        Outputs[0].Name = "Output(B)";
        return true;
    }

    // update the output pin
}


IntegerNode::IntegerNode( int& inId )
    : Node( inId++, "Integer", { 68, 201, 156 } )
{
    Outputs.emplace_back( inId++, "Value", PinType::Int );

    BuildNode();
}

bool IntegerNode::OnEvaluate()
{
    Outputs[0].Data = value;
    Outputs[0].Name = std::string( "Value(" + std::to_string( value ) + ")" ).c_str();
    // update the output pin
    return false;
}

bool IntegerNode::OnRender()
{
    if( Outputs[0].LinkedInput )
    {
        return false;
    }
    ImGui::PushItemWidth( 100 );
    if( ImGui::InputInt( "Value", &value ) )
    {
    }
    ImGui::PopItemWidth();

    return true;
}

void IntegerNode::OnExport( ShaderWriter& inFile )
{
    inFile.WriteInt( value );
}

FloatNode::FloatNode( int& inId )
    : Node( inId++, "Float", { 68, 101, 156 } )
{
    Outputs.emplace_back( inId++, "Value", PinType::Float );

    BuildNode();
}

bool FloatNode::OnEvaluate()
{
    Outputs[0].Data = value;
    Outputs[0].Name = std::string( "Value(" + std::to_string( value ) + ")" ).c_str();
    // update the output pin
    return false;
}

bool FloatNode::OnRender()
{
    if( Outputs[0].LinkedInput )
    {
        return false;
    }
    ImGui::PushItemWidth( 100 );
    if( ImGui::InputFloat( "Value", &value ) )
    {
    }
    ImGui::PopItemWidth();

    return true;
}

void FloatNode::OnExport( ShaderWriter& inFile )
{
    inFile.WriteFloat( value );
}

Vector3Node::Vector3Node( int& inId )
    : Node( inId++, "Vector 3", { 68, 201, 156 } )
{
    Inputs.emplace_back( inId++, "X(1)", PinType::Float );
    Inputs.back().Data = 0.f;
    Inputs.emplace_back( inId++, "Y(1)", PinType::Float );
    Inputs.back().Data = 0.f;
    Inputs.emplace_back( inId++, "Z(1)", PinType::Float );
    Inputs.back().Data = 0.f;
    Outputs.emplace_back( inId++, "Value(3)", PinType::Vector3Type );
    Outputs.back().Data = Vector3();

    BuildNode();
}

bool Vector3Node::OnEvaluate()
{
    for( int i = 0; i < Inputs.size(); ++i )
    {
        value[i] = Inputs[i].LinkedInput ? std::get<float>( Inputs[i].LinkedInput->Data ) : value[i];
    }
    Outputs[0].Data = value;
    // update the output pin
    return false;
}

bool Vector3Node::OnRender()
{
    bool isLinked = true;
    for( int i = 0; i < Inputs.size(); ++i )
    {
        isLinked = isLinked && Inputs[i].LinkedInput;
    }
    if( !isLinked && HavanaUtils::EditableVector3( "Value", value, 0.f) )
    {
    }

    return true;
}

void Vector3Node::OnExport( ShaderWriter& inFile )
{
    inFile.WriteVector( value );
}


AddNode::AddNode( int& inId )
    : Node( inId++, "Add", { 168, 201, 156 } )
{
    Inputs.emplace_back( inId++, "A(3)", PinType::Vector3Type );
    Inputs.back().Data = 0.f;
    Inputs.emplace_back( inId++, "B(3)", PinType::Vector3Type );
    Inputs.back().Data = 0.f;
    Outputs.emplace_back( inId++, "Value(3)", PinType::Vector3Type );
    Outputs.back().Data = Vector3();

    BuildNode();
}

bool AddNode::OnEvaluate()
{
    Vector3 result = ( Inputs[0].LinkedInput ? std::get<Vector3>( Inputs[0].LinkedInput->Data ) : valueA )
        + ( Inputs[1].LinkedInput ? std::get<Vector3>( Inputs[1].LinkedInput->Data ) : valueB );
    Outputs[0].Data = result;
    // update the output pin
    return false;
}

bool AddNode::OnRender()
{
    if( HavanaUtils::EditableVector3( "##Value A", valueA, 0.f ) )
    {
    }
    if( HavanaUtils::EditableVector3( "##Value B", valueA, 0.f ) )
    {
    }

    return true;
}

void AddNode::OnExport( ShaderWriter& inFile )
{
    std::string addNameA;
    std::string addNameB;

    // Make this a helper
    if( !ExportLinkedPin( 0, inFile ) )
    {
        inFile.WriteVector( valueA );
    }
    addNameA = inFile.LastVariable;
    // Make this a helper
    if( !ExportLinkedPin( 1, inFile ) )
    {
        inFile.WriteVector( valueB );
    }
    addNameB = inFile.LastVariable;

    std::string var = "v3_" + std::to_string( inFile.ID++ );
    inFile.WriteLine( "vec3 " + var + " = " + addNameA + " + " + addNameB + ";" );
    inFile.LastVariable = var;
}


SampleTextureNode::SampleTextureNode( int& inId )
    : Node( inId++, "Sample", { 168, 201, 156 } )
{
    Inputs.emplace_back( inId++, "Texture", PinType::Texture );
    Inputs.back().Data = 0.f;
    Inputs.emplace_back( inId++, "UV(2)", PinType::Vector2 );
    Inputs.back().Data = Vector2();
    Outputs.emplace_back( inId++, "Value(3)", PinType::Vector3Type );
    Outputs.back().Data = Vector3();

    BuildNode();
}

bool SampleTextureNode::OnEvaluate()
{
    //Vector3 result = ( Inputs[0].LinkedInput ? std::get<Vector3>( Inputs[0].LinkedInput->Data ) : valueA )
    //    + ( Inputs[1].LinkedInput ? std::get<Vector3>( Inputs[1].LinkedInput->Data ) : valueB );
    //Outputs[0].Data = result;
    // update the output pin
    return false;
}

bool SampleTextureNode::OnRender()
{
    if( ImGui::Button( "Select Texture" ) )
    {

        Path path = HUB::ShowOpenFilePrompt( nullptr );
        if( path.Exists )
        {
            value = ResourceCache::GetInstance().Get<Moonlight::Texture>( path );
        }

        //PlatformUtils::OpenFile(Path("../../../Assets"));
        //RequestAssetSelectionEvent evt( [this]( Path selectedAsset ) {
        //    value = ResourceCache::GetInstance().Get<Moonlight::Texture>( selectedAsset );
        //    }, AssetType::Texture );
        //evt.Fire();
    }

    if( value )
    {
        ImGui::Image( value->TexHandle, { 50, 50 } );
    }

    return true;
}

void SampleTextureNode::OnExport( ShaderWriter& inFile )
{
    // Make this a helper
    std::string uvName;
    if( !ExportLinkedPin( 1, inFile ) )
    {
        inFile.WriteLine( "vec2 uvs = v_texcoord0 * s_tiling.xy;" );
        inFile.LastVariable = "uvs";
    }
    uvName = inFile.LastVariable;

    std::string var = "sample_" + std::to_string( inFile.ID++ );
    inFile.WriteLine( "vec4 " + var + " = texture2D(s_texDiffuse, " + uvName + ");" );
    inFile.LastVariable = var;
}


CommentNode::CommentNode( int& inId )
    : Node( inId++, "Comment", { 168, 201, 156 } )
{
    CommentTitle = "New Comment";
    Type = NodeType::Comment;
}


BasicShaderMasterNode::BasicShaderMasterNode( int& inId )
    : Node( inId++, "Basic Shader", { 68, 201, 156 } )
{
    Inputs.emplace_back( inId++, "Position(3)", PinType::Vector3Type );
    Inputs.back().Data = Vector3();
    Inputs.emplace_back( inId++, "Color(3)", PinType::Vector3Type );
    Inputs.back().Data = Vector3();
    Inputs.emplace_back( inId++, "Alpha(1)", PinType::Float );
    Inputs.back().Data = 1.f;
}

bool BasicShaderMasterNode::OnEvaluate()
{
    return false;
}

void BasicShaderMasterNode::ExportPin( int inPinNum, PinType inPinType )
{
    //if( Inputs[inPinNum].LinkedInput )
    //{
    //    Inputs[inPinNum].LinkedInput->Node->OnExport( inFile );
    //}
    //else
    //{
    //    auto actualPin = Inputs[inPinNum].LinkedInput ? Inputs[inPinNum].LinkedInput : &Inputs[inPinNum];
    //
    //    if( actualPin->Type == inPinType )
    //    {
    //        return true;
    //    }
    //}
    //return false;
}

void BasicShaderMasterNode::ExportShitty( const std::string& inShaderName )
{
    {
        ShaderWriter file( inShaderName + ".var" );
        file.WriteLine( "vec4 v_color0 : COLOR0 = vec4(1.0, 0.0, 0.0, 1.0);" );
        file.WriteLine( "vec3 v_normal : NORMAL = vec3( 0.0, 0.0, 1.0 );" );
        file.WriteLine( "vec2 v_texcoord0 : TEXCOORD0 = vec2( 0.0, 0.0 );" );
        file.Append( "\n" );
        file.WriteLine( "vec3 a_position : POSITION;" );
        file.WriteLine( "vec3 a_normal : NORMAL;" );
        file.WriteLine( "vec2 a_texcoord0 : TEXCOORD0;" );
        file.WriteLine( "vec3 a_tangent : TANGENT;" );
        file.WriteLine( "vec3 a_bitangent : BITANGENT;" );

        file.WriteToDisk();
    }

    {
        ShaderWriter file( inShaderName + ".vert" );
        file.WriteLine( "$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent" );
        file.WriteLine( "$output v_color0, v_normal, v_texcoord0" );
        file.Append( "\n" );
        file.WriteLine( "#include \"Common.sh\"" );
        file.Append( "\n" );
        file.WriteLine( "void main()" );
        file.WriteLine( "{" );
        file.Tabs++;
        {
            if( !ExportLinkedPin( 0, file ) )
            {
                file.LastVariable = "a_position";
            }
        }
        file.WriteLine( "gl_Position = mul(u_modelViewProj, vec4(" + file.LastVariable + ", 1.0) );" );
        file.WriteLine( "v_color0 = vec4(a_normal.x,a_normal.y,a_normal.z,1.0);" );
        file.Append( "\n" );
        file.WriteLine( "v_texcoord0 = a_texcoord0;" );
        file.WriteLine( "vec3 normal = a_normal.xyz;" );
        file.WriteLine( "v_normal = mul(u_model[0], vec4(normalize(normal), 0.0) ).xyz;" );
        file.Tabs--;
        file.WriteLine( "}" );

        file.WriteToDisk();
    }

    {
        ShaderWriter file( inShaderName + ".frag" );
        file.WriteLine( "$input v_color0, v_normal, v_texcoord0" );
        file.Append( "\n" );
        file.WriteLine( "#include \"Common.sh\"" );
        file.Append( "\n" );
        file.WriteLine( "SAMPLER2D(s_texDiffuse, 0);" );
        file.WriteLine( "SAMPLER2D(s_texNormal, 1);" );
        file.WriteLine( "SAMPLER2D(s_texAlpha, 2);" );
        file.Append( "\n" );
        file.WriteLine( "uniform vec4 s_diffuse;" );
        file.WriteLine( "uniform vec4 s_ambient;" );
        file.WriteLine( "uniform vec4 s_sunDirection;" );
        file.WriteLine( "uniform vec4 s_sunDiffuse;" );
        file.WriteLine( "uniform vec4 s_tiling;" );
        file.WriteLine( "uniform vec4 u_skyLuminance;" );
        file.Append( "\n" );
        file.WriteLine( "void main()" );
        file.WriteLine( "{" );
        file.Tabs++;

        OnExport( file );
        std::string colorVariable;
        std::string alphaVariable;
        {
            if( !ExportLinkedPin( 1, file ) )
            {
                Vector3 data = std::get<Vector3>( Inputs[1].Data );
                file.WriteVector( data );
            }
            colorVariable = file.LastVariable;
        }
        {
            if( !ExportLinkedPin( 2, file ) )
            {
                // could this be trimmed even more?
                float data = std::get<float>( Inputs[2].Data );
                file.WriteFloat( data );
            }
            alphaVariable = file.LastVariable;
        }

        file.WriteLine( "vec2 uvs = v_texcoord0 * s_tiling.xy;" );
        file.WriteLine( "vec4 color = texture2D(s_texDiffuse, uvs) * vec4(" + colorVariable + ", " + alphaVariable + "); " );
        file.Append( "\n" );
        file.WriteLine( "vec4 ambient = s_ambient * color;" );
        file.WriteLine( "vec4 lightDir = normalize(s_sunDirection);" );
        file.WriteLine( "vec3 skyDirection = vec3(0.0, 0.0, 1.0);" );
        file.Append( "\n" );
        file.WriteLine( "float diff = max(dot(normalize(v_normal), lightDir), 0.0);" );
        file.WriteLine( "float diffuseSky = 1.0 + 0.5 * dot(normalize(v_normal), skyDirection);" );
        file.WriteLine( "diffuseSky *= 0.03;" );
        file.WriteLine( "vec4 diffuse = diff * s_sunDiffuse;// * color;" );
        file.WriteLine( "color += diffuse;" );
        file.Append( "\n" );
        file.WriteLine( "color += diffuseSky * u_skyLuminance;" );
        file.WriteLine( "color.a = toLinear(texture2D(s_texAlpha, uvs)).r * " + alphaVariable + ";" );
        file.WriteLine( "gl_FragColor = color;//texture2D(s_texNormal, uvs);//color;ambient + " );
        file.Tabs--;
        file.WriteLine( "}" );

        file.WriteToDisk();
    }
}

void BasicShaderMasterNode::OnExport( ShaderWriter& inFile )
{

}
