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
        : Program( BGFX_INVALID_HANDLE )
        , isLoaded( false )
    {
    }

    ShaderCommand::ShaderCommand( const std::string& InShaderFile )
    {
        OPTICK_EVENT( "ShaderCommand(string)" );

        Program = Moonlight::LoadProgram( InShaderFile + ".vert", InShaderFile + ".frag" );

        isLoaded = bgfx::isValid( Program );
    }

    ShaderCommand::ShaderCommand( const std::string& InVertexShaderPath, const std::string& InFragShaderPath )
    {
        Program = Moonlight::LoadProgram( InVertexShaderPath + ".vert", InFragShaderPath + ".frag" );

        isLoaded = bgfx::isValid( Program );
    }

    ShaderCommand::~ShaderCommand()
    {
        //bgfx::destroy(Program);
    }

}
