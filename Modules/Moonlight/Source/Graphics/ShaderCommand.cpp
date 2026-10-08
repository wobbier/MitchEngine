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

        SetProgram( Moonlight::LoadProgram( InShaderFile + ".vert", InShaderFile + ".frag" ) );
    }

    ShaderCommand::ShaderCommand( const std::string& InVertexShaderPath, const std::string& InFragShaderPath )
    {
        SetProgram( Moonlight::LoadProgram( InVertexShaderPath + ".vert", InFragShaderPath + ".frag" ) );
    }


    void ShaderCommand::SetProgram( bgfx::ProgramHandle inProgram )
    {
        Program = inProgram;
        isLoaded = bgfx::isValid( Program );
        m_programRef = isLoaded ? MakeShared<ProgramRef>( Program ) : nullptr;
    }


    ShaderCommand::ProgramRef::ProgramRef( bgfx::ProgramHandle inHandle )
        : Handle( inHandle )
    {
    }


    ShaderCommand::ProgramRef::~ProgramRef()
    {
        bgfx::destroy( Handle );
    }

}
