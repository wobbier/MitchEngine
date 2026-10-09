#include "ShaderCommand.h"

#include "optick.h"
#include "Utils/BGFXUtils.h"

namespace Moonlight
{
    //hlsl files - ShaderFile
    //shader asset - shader
    //shader backend - shaderdata
    //
    //shader has a shader file and shader data
    //
    //shader has shader type
    //
    //editor loads mesh with default shader
    //	shader stuff exported on mesh component
    //
    //mesh loads, created default shader
    //, editor loads on top points to new shader
    ShaderCommand::ShaderCommand()
    {
    }


    ShaderCommand::ShaderCommand( const std::string& InShaderFile )
    {
        OPTICK_EVENT( "ShaderCommand(string)" );
        m_programRef = Moonlight::AcquireProgram( InShaderFile + ".vert", InShaderFile + ".frag" );
    }


    ShaderCommand::ShaderCommand( const std::string& InVertexShaderPath, const std::string& InFragShaderPath )
    {
        m_programRef = Moonlight::AcquireProgram( InVertexShaderPath + ".vert", InFragShaderPath + ".frag" );
    }
}
