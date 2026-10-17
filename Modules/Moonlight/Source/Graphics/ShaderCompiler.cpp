#include "ShaderCompiler.h"
#include "Path.h"
#include "Utils/BGFXUtils.h"
#include "Utils/PlatformUtils.h"
#include "Dementia.h"

namespace Moonlight::ShaderCompiler
{
    namespace
    {
        std::string Quote( const std::string& InText )
        {
            return "\"" + InText + "\"";
        }


        std::string CompilerPath()
        {
#if USING( ME_PLATFORM_WIN64 )
            return Path( "Engine/Tools/Win64/shaderc.exe" ).FullPath;
#elif USING( ME_PLATFORM_MACOS )
            return Path( "Engine/Tools/macOS/shaderc" ).FullPath;
#else
            // Picks a rebuilt shaderc when there is one and works around stale Nix loaders.
            return Path( "Engine/Tools/RunShaderCompiler.sh" ).FullPath;
#endif
        }


        std::string HostPlatform()
        {
#if USING( ME_PLATFORM_WIN64 )
            return "windows";
#elif USING( ME_PLATFORM_MACOS )
            return "osx";
#else
            return "linux";
#endif
        }


        std::string DefaultVaryingDef( const std::string& InSourcePath )
        {
            const size_t slash = InSourcePath.find_last_of( "/\\" );
            const std::string directory = slash == std::string::npos ? std::string() : InSourcePath.substr( 0, slash + 1 );
            const std::string fileName = slash == std::string::npos ? InSourcePath : InSourcePath.substr( slash + 1 );
            return directory + fileName.substr( 0, fileName.rfind( '.' ) ) + ".var";
        }
    }


    Result Compile( const Request& InRequest )
    {
        const Profile profile = InRequest.TargetProfile.Name.empty() ? GetRunningProfile() : InRequest.TargetProfile;
        const std::string varyingDef = InRequest.VaryingDefPath.empty() ? DefaultVaryingDef( InRequest.SourcePath ) : InRequest.VaryingDefPath;

        std::string command = Quote( CompilerPath() );
        command += " -f " + Quote( InRequest.SourcePath );
        command += " -o " + Quote( InRequest.OutputPath );
        command += " --varyingdef " + Quote( varyingDef );
        command += " -i " + Quote( Path( "Engine/Assets/Shaders" ).FullPath );
        for( const std::string& include : InRequest.IncludeDirs )
        {
            command += " -i " + Quote( include );
        }
        command += " --platform " + profile.Platform + " -p " + profile.Name;
        command += std::string( " --type " ) + ( InRequest.ShaderStage == Stage::Vertex ? "vertex" : "fragment" );
        if( InRequest.WriteDepends )
        {
            command += " --depends";
        }

        Result result;
        result.Success = PlatformUtils::RunCommand( command, result.Log ) == 0;
        return result;
    }


    Profile GetRunningProfile()
    {
        const std::string renderer = Moonlight::GetPlatformString();
        if( renderer == "dx11" )
        {
            return { HostPlatform(), "s_5_0" };
        }
        if( renderer == "glsl" )
        {
            return { HostPlatform(), "430" };
        }
        if( renderer == "metal" )
        {
            return { HostPlatform(), "metal" };
        }
        if( renderer == "essl" )
        {
            return { HostPlatform(), "320_es" };
        }
        return { HostPlatform(), "spirv" };
    }


    std::vector<Profile> GetPortableProfiles()
    {
        std::vector<Profile> profiles = { { "linux", "spirv" }, { "linux", "430" }, { "osx", "metal" } };
#if USING( ME_PLATFORM_WIN64 )
        profiles.push_back( { "windows", "s_5_0" } );
#endif
        return profiles;
    }


    Stage StageFromPath( const std::string& InPath )
    {
        const size_t dot = InPath.rfind( '.' );
        return ( dot != std::string::npos && InPath.compare( dot, std::string::npos, ".vert" ) == 0 ) ? Stage::Vertex : Stage::Fragment;
    }
}
