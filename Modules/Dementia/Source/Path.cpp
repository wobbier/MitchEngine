#include "Path.h"

#include <algorithm>
#include "Dementia.h"
#include "CLog.h"
#include "VFS/VFS.h"

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
#if USING( ME_PLATFORM_UWP )
    // UWP keeps the pre-VFS flow: package-relative paths, no filesystem probing.
    std::string LocalPath;
    size_t pos;
    char buf[1024];
    GetModuleFileNameA( NULL, buf, 1024 );
    std::string ProgramPath( buf );
    std::replace( ProgramPath.begin(), ProgramPath.end(), '\\', '/' );
    pos = ProgramPath.find_last_of( "/" );
    ProgramPath = ProgramPath.substr( 0, pos + 1 );

    std::string assetPrefix;
    LocalPath = InFile;

    std::replace( LocalPath.begin(), LocalPath.end(), '\\', '/' );

    if( LocalPath.empty() )
    {
        return;
    }

    auto remove_duplicate_slashes = []( std::string& str ) {
        std::string::size_type slashPos = 0;
        while( ( slashPos = str.find( "//", slashPos ) ) != std::string::npos ) {
            str.replace( slashPos, 2, "/" );
        }
        };
    remove_duplicate_slashes( LocalPath );

    size_t path = LocalPath.find( ':' );
    if( path != std::string::npos )
    {
        FullPath = LocalPath;
    }
    else
    {
        FullPath = ProgramPath + assetPrefix + LocalPath;
    }

    const size_t extensionPos = LocalPath.rfind( '.' );
    ExtensionPos = extensionPos == std::string::npos ? 0 : LocalPath.size() - extensionPos - 1;

    path = LocalPath.rfind( "Assets" );
    if( path != std::string::npos )
    {
        LocalPath = LocalPath.substr( path, LocalPath.size() );
    }

    // Rough till I look up how UWP validates files
    Exists = true;

    const size_t localPos = FullPath.rfind( LocalPath );
    LocalPos = localPos == std::string::npos ? 0 : localPos;

    const size_t directoryEnd = FullPath.find_last_of( "/" );
    DirectoryPos = directoryEnd == std::string::npos ? FullPath.size() : FullPath.size() - directoryEnd - 1;

    FullPath = LocalPath;
#else
    std::string LocalPath = InFile;

    std::replace( LocalPath.begin(), LocalPath.end(), '\\', '/' );

    if( LocalPath.empty() )
    {
        return;
    }

    // Collapse duplicate slashes, keeping a scheme's "://" intact.
    const std::string scheme = VFS::GetScheme( LocalPath );
    std::string::size_type slashPos = scheme.size();
    while( ( slashPos = LocalPath.find( "//", slashPos ) ) != std::string::npos )
    {
        LocalPath.replace( slashPos, 2, "/" );
    }

    const size_t extensionPos = LocalPath.rfind( '.' );
    ExtensionPos = extensionPos == std::string::npos ? 0 : LocalPath.size() - extensionPos - 1;

#if USING( ME_PLATFORM_MACOS ) || USING( ME_PLATFORM_LINUX )
    const bool absolute = scheme.empty() && LocalPath[0] == '/';
#else
    const bool absolute = scheme.empty() && LocalPath.find( ':' ) != std::string::npos;
#endif
    if( absolute )
    {
        // Absolute paths bypass the mounts.
        FullPath = LocalPath;
        std::error_code ec;
        const std::filesystem::file_status status = std::filesystem::status( FullPath, ec );
        Exists = !ec && std::filesystem::exists( status );
        IsFile = Exists && std::filesystem::is_regular_file( status );
        IsFolder = !IsFile;

        const size_t assetsPos = LocalPath.rfind( "Assets" );
        if( assetsPos != std::string::npos )
        {
            LocalPath = LocalPath.substr( assetsPos );
        }
    }
    else
    {
        VfsResolved resolved = VFS::Get().Resolve( LocalPath, Raw );
        // Legacy: a missing engine-assets path redirects into the game tree, so writes
        // against engine content land in the project instead of the engine.
        if( resolved.Resolved && !resolved.Exists && !Raw && LocalPath.rfind( "Engine/Assets", 0 ) == 0 )
        {
            resolved = VFS::Get().Resolve( LocalPath.substr( 7 ), Raw );
        }
        FullPath = std::move( resolved.PhysicalPath );
        Exists = resolved.Exists;
        IsFile = resolved.IsFile;
        IsFolder = !IsFile;
        LocalPath = std::move( resolved.DisplayPath );
    }

    const size_t localPos = FullPath.rfind( LocalPath );
    LocalPos = localPos == std::string::npos ? 0 : localPos;

    const size_t directoryEnd = FullPath.find_last_of( "/" );
    DirectoryPos = directoryEnd == std::string::npos ? FullPath.size() : FullPath.size() - directoryEnd - 1;
#endif
}

Path::~Path()
{

}

const char* Path::GetExtension() const
{
    const char* c = FullPath.c_str();
    if( ExtensionPos > FullPath.size() )
    {
        return c + FullPath.size();
    }
    return &c[FullPath.size() - ExtensionPos];
}

std::string_view Path::GetDirectory() const
{
    if( DirectoryPos > FullPath.size() )
    {
        return {};
    }
    return std::string_view( FullPath.c_str(), FullPath.size() - DirectoryPos );
}

std::string Path::GetDirectoryString() const
{
    return std::string( GetDirectory() );
}

std::string_view Path::GetLocalPath() const
{
    if( LocalPos > FullPath.size() )
    {
        return {};
    }
    return std::string_view( FullPath.c_str() + LocalPos, FullPath.size() - LocalPos );
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
