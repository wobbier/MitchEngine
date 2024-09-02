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
#include "optick.h"

Path::Path( const std::string& InFile, bool Raw /*= false*/ )
{
    OPTICK_CATEGORY( "Path", Optick::Category::IO );
    std::string LocalPath;
#if USING( ME_PLATFORM_UWP )
    size_t pos;
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
#if USING( ME_EDITOR ) && USING( ME_PLATFORM_WIN64 )
    assetPrefix = "/";
#elif USING( ME_PLATFORM_UWP )
#elif USING( ME_PLATFORM_MACOS )
#if USING( ME_EDITOR )
    // #TODO: WTF
    assetPrefix = "/";
#else
    assetPrefix = "/";
#endif
#else
    assetPrefix = "/";
#endif
    LocalPath = InFile;

    std::replace( LocalPath.begin(), LocalPath.end(), '\\', '/' );

    auto remove_duplicate_slashes = []( std::string& str ) {
        std::string::size_type pos = 0;
        while( ( pos = str.find( "//", pos ) ) != std::string::npos ) {
            str.replace( pos, 2, "/" );
        }
        };
    remove_duplicate_slashes( LocalPath );

#if USING( ME_PLATFORM_MACOS )
    size_t path = LocalPath[0] == '/' ? 0 : std::string::npos;
#else
    size_t path = LocalPath.find( ':' );
#endif
    if( path != std::string::npos )
    {
        FullPath = LocalPath;
    }
    else
    {
        FullPath = ProgramPath + assetPrefix + LocalPath;
    }

    ExtensionPos = (int8_t)LocalPath.rfind( '.' );
    ExtensionPos = (int8_t)( LocalPath.size() - ++ExtensionPos );

    // yeah don't do this
    path = LocalPath.rfind( "Assets" );
    if( path != std::string::npos )
    {
        LocalPath = LocalPath.substr( path, LocalPath.size() );
    }

#if USING( ME_EDITOR ) || USING( ME_PLATFORM_MACOS ) || USING( ME_PLATFORM_WIN64 )
    if( !std::filesystem::exists( FullPath ) )
    {
        if( !Raw )
        {
            std::string tempPath = ProgramPath + assetPrefix + "Engine/" + LocalPath;
            if( std::filesystem::exists( tempPath ) )
            {
                FullPath = std::move( tempPath );
                assetPrefix = assetPrefix.append( "Engine/" );
                LocalPath = "Engine/" + LocalPath;
                Exists = true;
            }
            else
            {
                auto pos = FullPath.rfind( "Engine/Assets" );
                if( pos != std::string::npos )
                {
                    // Gross
                    FullPath.replace( pos, 13, "Assets" );
                }
            }
        }
    }
    else
    {
        Exists = true;
    }

    if( std::filesystem::is_regular_file( FullPath ) )
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
    LocalPos = static_cast<int8_t>( FullPath.rfind( LocalPath ) );
    DirectoryPos = static_cast<int8_t>( FullPath.find_last_of( "/" ) + 1 );
    DirectoryPos = (int8_t)( FullPath.size() - DirectoryPos );

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

std::string_view Path::GetDirectory() const
{
    return std::string_view( FullPath.c_str(), FullPath.size() - DirectoryPos );
}

std::string Path::GetDirectoryString() const
{
    return std::string( GetDirectory() );
}

std::string_view Path::GetLocalPath() const
{
    return std::string_view( FullPath.c_str() + LocalPos );
}

std::string Path::GetLocalPathString() const
{
    return std::string( GetLocalPath() );
}

std::string_view Path::GetFileName( bool inIncludeExt ) const
{
    std::string_view localPath = GetLocalPath();
    size_t lastSlash = localPath.rfind( '/' );
    if( lastSlash != std::string::npos )
    {
        lastSlash++;
    }
    else
    {
        lastSlash = 0;
    }

    if( inIncludeExt )
    {
        return localPath.substr( lastSlash, localPath.size() );
    }
    else
    {
        //size_t period = GetLocalPathString().rfind( '.' );
        size_t size = localPath.size() - lastSlash - ExtensionPos - 1;

        return localPath.substr( lastSlash, size );
    }
}

std::string Path::GetFileNameString( bool inIncludeExt ) const
{
    return std::string( GetFileName( inIncludeExt ) );
}
