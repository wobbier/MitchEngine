#pragma once
#include "Resource/Resource.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "Resource/MetaRegistry.h"
#include <string>
#include "Utils/StringUtils.h"
#include "Utils/BGFXUtils.h"
#include "bx/readerwriter.h"
#include <Utils/PlatformUtils.h>
#include <bgfx/bgfx.h>
#include "Core/Assert.h"

namespace Moonlight
{
    class ShaderFile
        : public Resource
    {
    public:
        ShaderFile( const Path& InPath )
            : Resource( InPath )
        {
            std::string path = FilePath.FullPath.substr( FilePath.FullPath.rfind( "/" ) + 1, FilePath.FullPath.length() );
            Path fullPath = Path(std::string( FilePath.GetDirectory() ) + path + "." + Moonlight::GetPlatformString() + ".bin");
            
            //fullPath = fullPath.substr(0, fullPath.rfind(".")) + ".bin";
            ME_ASSERT_MSG( fullPath.Exists, "Shader doesn't exist." );
            Data = Moonlight::LoadMemory( fullPath );
            Handle = bgfx::createShader( Data );
            bgfx::setName( Handle, InPath.GetLocalPath().data() );
        }

        inline std::vector<char> ReadToByteArray( const char* filename )
        {
            std::vector<char> data;
            std::ifstream file( filename, std::ios::in | std::ios::binary | std::ios::ate );
            data.resize( file.tellg() );
            file.seekg( 0, std::ios::beg );
            file.read( &data[0], data.size() );
            return data;
        }

        const bgfx::Memory* Data = nullptr;
        bgfx::ShaderHandle Handle = BGFX_INVALID_HANDLE;
    };
}

struct ShaderFileMetadata
    : public MetaBase
{
    ShaderFileMetadata( const Path& filePath )
        : MetaBase( filePath )
    {

    }

    virtual std::string GetExtension2() const override
    {
        return Moonlight::GetPlatformString() + std::string( ".bin" );
    }

    void OnSerialize( json& outJson ) override
    {
    }

    void OnDeserialize( const json& inJson ) override
    {
    }

    void Export() override
    {
#if USING( ME_PLATFORM_WIN64 )
        Path shadercPath = Path( "Engine/Tools/Win64/shaderc.exe" );

        std::string exportType;
        std::string shaderType;
        const std::string ext = FilePath.GetExtension();
        if( ext == "frag" )
        {
            exportType = "fragment";
            shaderType = "s_5_0";
        }
        else if( ext == "vert" )
        {
            exportType = "vertex";
            shaderType = "s_5_0";
        }

        std::string fileName = std::string( FilePath.GetLocalPath().substr( FilePath.GetLocalPath().rfind( "/" ) + 1, FilePath.GetLocalPath().length() ) );
        std::string localFolder = FilePath.GetDirectoryString();

        // --platform windows -p vs_5_0 -O 3 --type vertex --depends -o $(@) -f $(<) --disasm
        //
        std::string nameNoExt = fileName.substr( 0, fileName.rfind( "." ) );
        std::string progArgs = "-f \"" + localFolder + fileName + "\"" + " -o \"" + localFolder + fileName + "." + GetExtension2() + "\" --varyingdef \"" + localFolder + nameNoExt + ".var\" --platform windows -p " + shaderType + " --type " + exportType;
        progArgs += " -i \"" + Path( "Engine/Assets/Shaders" ).FullPath + "\"";
        // ./shaderc -f ../../../Assets/Shaders/vs_cubes.shader -o ../../../Assets/Shaders/dummy.bin --varyingdef ./varying.def.sc --platform windows -p vs_5_0 --type vertex

        PlatformUtils::SystemCall( shadercPath, progArgs );

#elif USING( ME_PLATFORM_MACOS )
        Path shadercPath = Path( "Engine/Tools/macOS/shaderc" );

        std::string exportType;
        std::string shaderType = "ps_5_0";
        std::string ext = FilePath.GetExtension();
        if( ext == "frag" )
        {
            exportType = "fragment";
        }
        else if( ext == "vert" )
        {
            exportType = "vertex";
        }

        // #TODO: Do I need this local string shit?
        std::string fileName = FilePath.GetLocalPathString().substr( FilePath.GetLocalPathString().rfind( "/" ) + 1, FilePath.GetLocalPathString().length() );
        std::string localFolder = FilePath.GetLocalPathString().substr( 0, FilePath.GetLocalPathString().rfind( "/" ) + 1 );

        std::string nameNoExt = fileName.substr( 0, fileName.rfind( "." ) );
        std::string progArgs = "\"" + shadercPath.FullPath + "\" -f "+shadercPath.GetDirectoryString()+"/../../../";
        progArgs += localFolder + fileName;
        progArgs += " -o "+shadercPath.GetDirectoryString()+"/../../../" + localFolder + fileName + "." + GetExtension2() + " --varyingdef "+shadercPath.GetDirectoryString()+"/../../../" + localFolder + nameNoExt + ".var --platform osx -p metal --depends -disasm --type " + exportType;


        //"\"/Users/mitchellandrews/Projects/stack-new/Engine/Tools/macOS/shaderc\" -f ../../Engine/Assets/Shaders/UI.vert -o ../../Engine/Assets/Shaders/UI.vert.metal.bin --varyingdef ../../Engine/Assets/Shaders/UI.var --platform osx -p metal --depends -disasm --type vertex"
        // texturec -f $in -o $out -t bc2 -m
        system( progArgs.c_str() );
#endif
    }
#if USING( ME_EDITOR )
    void OnEditorInspect() override
    {
    }
#endif
};

struct FragShaderFileMetadata
    : public ShaderFileMetadata
{
    FragShaderFileMetadata( const Path& filePath )
        : ShaderFileMetadata( filePath )
    {
    }
};

ME_REGISTER_METADATA( "vert", ShaderFileMetadata );
ME_REGISTER_METADATA( "frag", FragShaderFileMetadata );
