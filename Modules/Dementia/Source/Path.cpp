#include "Path.h"

#include <algorithm>
#include "Dementia.h"
#include "CLog.h"

#if USING( ME_PLATFORM_UWP )
#include <wrl/client.h>
#include <fstream>
#else
#include <filesystem>
#endif

Path::Path( const std::string& InFile, bool Raw /*= false*/ )
{
    size_t pos;
#if USING( ME_PLATFORM_UWP )
    char buf[1024];
    GetModuleFileNameA( NULL, buf, 1024 );
    std::string ProgramPath( buf );
    std::replace( ProgramPath.begin(), ProgramPath.end(), '\\', '/' );
    pos = ProgramPath.find_last_of( "/" );
    ProgramPath = ProgramPath.substr( 0, pos + 1 );
#else
    auto p = std::filesystem::current_path();
    std::string ProgramPath( std::string( p.generic_string() ) );
    std::replace( ProgramPath.begin(), ProgramPath.end(), '\\', '/' );
#endif

    std::string assetPrefix;
#if ME_EDITOR && USING( ME_PLATFORM_WIN64 )
    assetPrefix = "/";
#elif USING( ME_PLATFORM_UWP )
#elif USING( ME_PLATFORM_MACOS )
#if ME_EDITOR
    assetPrefix = "/../../";
#else
    assetPrefix = "/";
#endif
#else
    assetPrefix = "/";
#endif
    LocalPath = InFile;

    std::replace( LocalPath.begin(), LocalPath.end(), '\\', '/' );
#if USING( ME_PLATFORM_MACOS )
    size_t path = LocalPath[0] == '/' ? 0 : std::string::npos;
#else
    size_t path = LocalPath.find( ':' );
#endif
    if ( path != std::string::npos )
    {
        FullPath = LocalPath;
    }
    else
    {
        FullPath = ProgramPath + assetPrefix + LocalPath;
    }

    ExtensionPos = (int8_t)LocalPath.rfind( '.' );
    ExtensionPos = (int8_t)( LocalPath.size() - ++ExtensionPos );

    path = LocalPath.rfind( "Assets" );
    if ( path != std::string::npos )
    {
        LocalPath = LocalPath.substr( path, LocalPath.size() );
    }

#if ME_EDITOR || USING( ME_PLATFORM_MACOS )
    if ( !std::filesystem::exists( FullPath ) )
    {
        if ( !Raw )
        {
            std::string tempPath = ProgramPath + assetPrefix + "Engine/" + LocalPath;
            if ( std::filesystem::exists( tempPath ) )
            {
                FullPath = std::move( tempPath );
                assetPrefix = assetPrefix.append( "Engine/" );
                LocalPath = "Engine/" + LocalPath;
                Exists = true;
            }
        }
    }
    else
    {
        Exists = true;
    }

    if ( std::filesystem::is_regular_file( FullPath ) )
    {
        IsFile = true;
    }
    else
    {
        IsFolder = true;
    }
#else

#if USING( ME_PLATFORM_UWP )
        // Rough till I look up how UWP validates files
    Exists = true;
#else
    Exists = std::filesystem::exists( FullPath );
#endif

#endif
    pos = FullPath.find_last_of( "/" );
    Directory = FullPath.substr( 0, pos + 1 );

#if USING( ME_PLATFORM_UWP )
        //std::replace(LocalPath.begin(), LocalPath.end(), '/', '\\');
    FullPath = LocalPath;
#endif
}

Path::~Path()
{

}

const char* Path::GetExtension() const
{
    const char* c = FullPath.c_str();
    return &c[FullPath.size() - ExtensionPos];
}

