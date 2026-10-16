#pragma once
#include "Resource/Resource.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "Resource/MetaRegistry.h"
#include <string>
#include <cstring>
#include <vector>
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
        }

        // ResourceCache::Get: read and create in one go.
        bool Load() override
        {
            return LoadAsync() && FinishAsyncLoad();
        }

        // ResourceCache::GetAsync: a first-time cook (shaderc) and the binary read run on a loader
        // thread; the shader and the programs waiting for it are created on the main thread.
        bool SupportsAsyncLoad() const override
        {
            return true;
        }

        // Loader thread: plain file IO (the shared bx reader isn't thread safe).
        bool LoadAsync() override
        {
            const Path binary = BinaryPath();
            std::ifstream file( binary.FullPath, std::ios::binary | std::ios::ate );
            if( !file )
            {
                YIKES( "Shader binary is missing: " + binary.FullPath );
                return false;
            }
            m_pendingBinary.resize( static_cast<size_t>( file.tellg() ) );
            file.seekg( 0 );
            return !m_pendingBinary.empty() && static_cast<bool>( file.read( m_pendingBinary.data(), static_cast<std::streamsize>( m_pendingBinary.size() ) ) );
        }

        bool FinishAsyncLoad() override
        {
            const std::vector<char> binary = std::move( m_pendingBinary );
            m_pendingBinary = {};
            if( binary.empty() )
            {
                return false;
            }
            if( !IsGpuAlive() )
            {
                return true;
            }
            // Null-terminated like LoadMemory: some backends read the binary as text.
            const bgfx::Memory* memory = bgfx::alloc( static_cast<uint32_t>( binary.size() + 1 ) );
            std::memcpy( memory->data, binary.data(), binary.size() );
            memory->data[binary.size()] = '\0';
            Handle = bgfx::createShader( memory );
            if( !bgfx::isValid( Handle ) )
            {
                return false;
            }
            bgfx::setName( Handle, FilePath.GetLocalPath().data() );
            Moonlight::RebuildProgramsUsing( this );
            return true;
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
            if( bgfx::isValid( Handle ) && IsGpuAlive() )
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

    private:
        std::vector<char> m_pendingBinary;
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

    // shaderc runs off the main thread on hot reload.
    bool ExportsInBackground() const override
    {
        return true;
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
        const Path shadercPath = Path( "Engine/Tools/RunShaderCompiler.sh" );
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
