#include "ShaderDependencies.h"
#include "Utils/BGFXUtils.h"
#include "Path.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Moonlight
{
    namespace
    {
        bool EndsWith( const std::string& InText, const std::string& InSuffix )
        {
            return InText.size() >= InSuffix.size() && InText.compare( InText.size() - InSuffix.size(), InSuffix.size(), InSuffix ) == 0;
        }

        std::string Normalize( const std::string& InPath )
        {
            std::error_code ec;
            const std::filesystem::path normalized = std::filesystem::weakly_canonical( std::filesystem::path( InPath ), ec );
            return ec ? std::filesystem::path( InPath ).lexically_normal().generic_string() : normalized.generic_string();
        }

        // Dependencies listed in a make-style depfile ("target : dep dep \ ...").
        std::vector<std::string> ReadDepfile( const std::filesystem::path& InPath )
        {
            std::ifstream file( InPath );
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string text = buffer.str();
            const size_t colon = text.find( " :" );
            std::vector<std::string> deps;
            if( colon == std::string::npos )
            {
                return deps;
            }
            std::replace( text.begin(), text.end(), '\\', ' ' );
            std::stringstream tokens( text.substr( colon + 2 ) );
            std::string token;
            while( tokens >> token )
            {
                deps.push_back( Normalize( token ) );
            }
            return deps;
        }
    }


    void ExpandShaderChanges( std::vector<std::string>& InOutChangedPaths )
    {
        std::vector<std::string> includes;
        std::vector<std::string> added;
        for( const std::string& changed : InOutChangedPaths )
        {
            if( EndsWith( changed, ".sh" ) )
            {
                includes.push_back( Normalize( changed ) );
            }
            else if( EndsWith( changed, ".var" ) )
            {
                const std::string base = changed.substr( 0, changed.size() - 4 );
                for( const char* extension : { ".vert", ".frag" } )
                {
                    if( std::filesystem::exists( base + extension ) )
                    {
                        added.push_back( base + extension );
                    }
                }
            }
        }

        if( !includes.empty() )
        {
            const std::string suffix = "." + GetPlatformString() + ".bin.d";
            for( const char* root : { "Engine/Assets/Shaders", "Assets/Shaders" } )
            {
                const Path rootPath( root, true );
                std::error_code ec;
                if( !std::filesystem::is_directory( rootPath.FullPath, ec ) )
                {
                    continue;
                }
                for( std::filesystem::recursive_directory_iterator it( rootPath.FullPath, ec ), end; !ec && it != end; it.increment( ec ) )
                {
                    const std::string depfile = it->path().generic_string();
                    if( !it->is_regular_file( ec ) || !EndsWith( depfile, suffix ) )
                    {
                        continue;
                    }
                    const std::vector<std::string> deps = ReadDepfile( it->path() );
                    const bool affected = std::any_of( deps.begin(), deps.end(), [&includes]( const std::string& dep ) {
                        return std::find( includes.begin(), includes.end(), dep ) != includes.end();
                    } );
                    const std::string source = depfile.substr( 0, depfile.size() - suffix.size() );
                    if( affected && std::filesystem::exists( source ) )
                    {
                        added.push_back( source );
                    }
                }
            }
        }

        for( const std::string& path : added )
        {
            if( std::find( InOutChangedPaths.begin(), InOutChangedPaths.end(), path ) == InOutChangedPaths.end() )
            {
                InOutChangedPaths.push_back( path );
            }
        }
    }
}
