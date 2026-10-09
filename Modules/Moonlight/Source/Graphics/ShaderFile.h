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
            const Path fullPath = BinaryPath();

            //fullPath = fullPath.substr(0, fullPath.rfind(".")) + ".bin";
            ME_ASSERT_MSG( fullPath.Exists, "Shader doesn't exist." );
            // createShader consumes the memory, so don't hold on to it.
            Handle = bgfx::createShader( Moonlight::LoadMemory( fullPath ) );
            bgfx::setName( Handle, InPath.GetLocalPath().data() );
        }

        // Recreates the shader from the freshly compiled binary and rebuilds every program using it
        // (ShaderCommand users see the new program immediately). Programs created from the old
        // shader keep their own reference, so destroying it here is safe.
        void Reload() override
        {
            const bgfx::Memory* memory = Moonlight::LoadMemory( BinaryPath() );
            if( !memory )
            {
                return;
            }
            const bgfx::ShaderHandle rebuilt = bgfx::createShader( memory );
            if( !bgfx::isValid( rebuilt ) )
            {
                return;
            }
            bgfx::setName( rebuilt, FilePath.GetLocalPath().data() );
            const bgfx::ShaderHandle previous = Handle;
            Handle = rebuilt;
            Moonlight::RebuildProgramsUsing( this );
            if( bgfx::isValid( previous ) )
            {
                bgfx::destroy( previous );
            }
        }

        Path BinaryPath() const
        {
            return Path( FilePath.FullPath + "." + Moonlight::GetPlatformString() + ".bin" );
        }

        // Programs hold their own reference to the shader, so this is safe while they're alive.
        ~ShaderFile()
        {
            if( bgfx::isValid( Handle ) )
            {
                bgfx::destroy( Handle );
            }
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

    // Compiles the shader with bgfx's shaderc for the running renderer, next to the source (absolute
    // paths, so engine shaders compile in place), with a depfile for include tracking. Compiler
    // errors go to the log.
    void Export() override
    {
#if USING( ME_PLATFORM_WIN64 )
        const Path shadercPath = Path( "Engine/Tools/Win64/shaderc.exe" );
        const char* platform = "windows";
#elif USING( ME_PLATFORM_MACOS )
        const Path shadercPath = Path( "Engine/Tools/macOS/shaderc" );
        const char* platform = "osx";
#else
        const Path shadercPath = Path( "Engine/Tools/linux/shaderc" );
        const char* platform = "linux";
#endif
        const std::string extension = FilePath.GetExtension();
        const std::string type = extension == "frag" ? "fragment" : "vertex";

        const std::string renderer = Moonlight::GetPlatformString();
        std::string profile = "spirv";
        if( renderer == "dx11" )
        {
            profile = "s_5_0";
        }
        else if( renderer == "glsl" )
        {
            profile = "150";
        }
        else if( renderer == "metal" )
        {
            profile = "metal";
        }
        else if( renderer == "essl" )
        {
            profile = "320_es";
        }

        const std::string source = FilePath.FullPath;
        const std::string directory = FilePath.GetDirectoryString();
        std::string fileName = source.substr( source.find_last_of( "/\\" ) + 1 );
        const std::string baseName = fileName.substr( 0, fileName.rfind( '.' ) );
        auto quote = []( const std::string& InText ) { return "\"" + InText + "\""; };

        std::string command = quote( shadercPath.FullPath );
        command += " -f " + quote( source );
        command += " -o " + quote( source + "." + GetExtension2() );
        command += " --varyingdef " + quote( directory + baseName + ".var" );
        command += " -i " + quote( Path( "Engine/Assets/Shaders" ).FullPath );
        command += std::string( " --platform " ) + platform + " -p " + profile + " --type " + type + " --depends";

        std::string output;
        const int result = PlatformUtils::RunCommand( command, output );
        if( result != 0 )
        {
            YIKES( "Shader compile failed: " + FilePath.GetLocalPathString() + "\n" + output );
        }
        else
        {
            CLog::Log( CLog::LogType::Info, "Compiled shader: " + FilePath.GetLocalPathString() );
        }
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
