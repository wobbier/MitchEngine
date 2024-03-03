#include "BasicNodes.h"

LessThanNode::LessThanNode( int& inId )
    : Node( inId++, "Less Than" )
{
    Inputs.emplace_back( inId++, "A", PinType::Int );
    Inputs.emplace_back( inId++, "B", PinType::Int );
    Outputs.emplace_back( inId++, "Output", PinType::Int );

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

BasicShaderMasterNode::BasicShaderMasterNode( int& inId )
    : Node( inId++, "Basic Shader", { 68, 201, 156 } )
{
    Inputs.emplace_back( inId++, "Color", PinType::Int );
}

bool BasicShaderMasterNode::OnEvaluate()
{
    return false;
}

void BasicShaderMasterNode::ExportShitty( const std::string& inShaderName )
{
    {
        File file( Path( inShaderName + ".var" ) );
        file.Reset();
        file.Append( "vec4 v_color0 : COLOR0 = vec4(1.0, 0.0, 0.0, 1.0);\n\
vec3 v_normal : NORMAL = vec3( 0.0, 0.0, 1.0 );\n\
vec2 v_texcoord0 : TEXCOORD0 = vec2( 0.0, 0.0 );\n\
\n\
vec3 a_position : POSITION;\n\
vec3 a_normal : NORMAL;\n\
vec2 a_texcoord0 : TEXCOORD0;\n\
vec3 a_tangent : TANGENT;\n\
vec3 a_bitangent : BITANGENT;\n\
        " );
        //OnExport( file );

        file.Write();
    }
    {
        File file( Path( inShaderName + ".vert" ) );
        file.Reset();
        file.Append( "$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent\n\
$output v_color0, v_normal, v_texcoord0\n\
\n\
#include \"Common.sh\"\n\
\n\
void main()\n\
{\n\
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );\n\
	v_color0 = vec4(a_normal.x,a_normal.y,a_normal.z,1.0);\n\
	v_texcoord0 = a_texcoord0;\n\
\n\
	vec3 normal = a_normal.xyz;\n\
	v_normal = mul(u_model[0], vec4(normalize(normal), 0.0) ).xyz;\n\
}" );
        //OnExport( file );

        file.Write();
    }
    {
        File file( Path( inShaderName + ".frag" ) );
        file.Reset();
        file.Append( "$input v_color0, v_normal, v_texcoord0\n\
#include \"Common.sh\"\n\
\n\
SAMPLER2D(s_texDiffuse, 0);\n\
SAMPLER2D(s_texNormal, 1);\n\
SAMPLER2D(s_texAlpha, 2);\n\
uniform vec4 s_diffuse;\n\
uniform vec4 s_ambient;\n\
uniform vec4 s_sunDirection;\n\
uniform vec4 s_sunDiffuse;\n\
uniform vec4 s_tiling;\n\
uniform vec4 u_skyLuminance;\n\
\n\
void main()\n\
{\n\
	vec2 uvs = v_texcoord0 * s_tiling.xy;\n\
	vec4 color = texture2D(s_texDiffuse, uvs) * s_diffuse;\n\
\n\
	vec4 ambient = s_ambient * color;\n\
	vec4 lightDir = normalize(s_sunDirection);\n\
	vec3 skyDirection = vec3(0.0, 0.0, 1.0);\n\
\n\
	float diff = max(dot(normalize(v_normal), lightDir), 0.0);\n\
	float diffuseSky = 1.0 + 0.5 * dot(normalize(v_normal), skyDirection);\n\
	diffuseSky *= 0.03;\n\
	vec4 diffuse = diff * s_sunDiffuse;// * color;\n\
	color += diffuse;\n\
	color += diffuseSky * u_skyLuminance;\n\
	color.a = toLinear(texture2D(s_texAlpha, uvs)).r;\n\
	gl_FragColor = color;//texture2D(s_texNormal, uvs);//color;ambient + \n\
}" );
        ///OnExport( file );

        file.Write();
    }
}

void BasicShaderMasterNode::OnExport( File& inFile )
{
    for( auto pin : Inputs )
    {
        auto actualPin = pin.LinkedInput ? pin.LinkedInput : &pin;

        inFile.Append( "float var" + std::to_string( m_variableId++ ) + " = " + std::to_string(std::get<int>(actualPin->Data)) + ";\n");
    }
}
