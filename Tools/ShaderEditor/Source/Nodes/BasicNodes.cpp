#include "BasicNodes.h"
#include "imgui.h"
#include "Utils\HavanaUtils.h"

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
    std::string var = "int_" + std::to_string( inFile.ID++ );
    inFile.WriteLine( "float " + var + " = " + std::to_string( value ) );
    inFile.LastVariable = var;
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
    std::string var = "float" + std::to_string( inFile.ID++ );
    inFile.WriteLine( "float " + var + " = " + std::to_string( value ) );
    inFile.LastVariable = var;
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
    if( Outputs[0].LinkedInput )
    {
        return false;
    }
    Vector3();
    if( HavanaUtils::EditableVector3( "Value", value, 0.f, 300.f ) )
    {
    }

    return true;
}


void Vector3Node::OnExport( ShaderWriter& inFile )
{
    std::string var = "v_3_" + std::to_string( inFile.ID++ );
    inFile.WriteLine( "vec3 " + var + " = vec3( " + std::to_string( value.x ) + ", " + std::to_string( value.y ) + ", " + std::to_string( value.z ) + " );" );
    inFile.LastVariable = var;
}

BasicShaderMasterNode::BasicShaderMasterNode( int& inId )
    : Node( inId++, "Basic Shader", { 68, 201, 156 } )
{
    Inputs.emplace_back( inId++, "Position(3)", PinType::Vector3Type );
    Inputs.back().Data = Vector3();
    Inputs.emplace_back( inId++, "Color", PinType::Int );
    Inputs.back().Data = 0;
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
            if( Inputs[0].LinkedInput )
            {
                Inputs[0].LinkedInput->Node->OnExport( file );
            }
            else
            {
                auto actualPin = Inputs[0].LinkedInput ? Inputs[0].LinkedInput : &Inputs[0];

                if( actualPin->Type == PinType::Vector3Type )
                {
                    Vector3 data = std::get<Vector3>( actualPin->Data );
                    std::string variableName = "var_default" + std::to_string( file.ID++ );
                    file.WriteLine( "vec3 " + variableName + " = vec3( " + std::to_string( data.x ) + ", " + std::to_string( data.y ) + ", " + std::to_string( data.z ) + " );" );
                    file.LastVariable = variableName;
                }
            }
        }
        file.WriteLine( "v_color0 = vec4( " + file.LastVariable + ", 1.0 );" );
        file.WriteLine( "gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );" );
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
        file.WriteLine( "#include \"Common.sh" );
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

        file.WriteLine( "vec2 uvs = v_texcoord0 * s_tiling.xy;" );
        file.WriteLine( "vec4 color = texture2D(s_texDiffuse, uvs) * s_diffuse;" );
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
        file.WriteLine( "color.a = toLinear(texture2D(s_texAlpha, uvs)).r;" );
        file.WriteLine( "gl_FragColor = color;//texture2D(s_texNormal, uvs);//color;ambient + " );
        file.Tabs--;
        file.WriteLine( "}" );

        file.WriteToDisk();
    }
}

void BasicShaderMasterNode::OnExport( ShaderWriter& inFile )
{

    {
        if( Inputs[1].LinkedInput )
        {
            Inputs[1].LinkedInput->Node->OnExport( inFile );
        }
        else
        {
            auto actualPin = Inputs[1].LinkedInput ? Inputs[1].LinkedInput : &Inputs[1];

            // Temp Shit
            if( actualPin->Type == PinType::Int )
            {
                std::string variableName = "int" + std::to_string( inFile.ID++ );
                inFile.WriteLine( "float " + variableName + " = " + std::to_string( std::get<int>( actualPin->Data ) ) + ";" );
                inFile.LastVariable = variableName;
            }
        }
    }
}
