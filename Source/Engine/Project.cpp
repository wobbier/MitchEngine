#include "PCH.h"
#include "Project.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "Core/CommandLine.h"
#include "JSON.h"
#include "SDL.h"
#include "VFS/VFS.h"

namespace
{
    namespace fs = std::filesystem;

    // A file only the engine has: game projects carry their own Assets/Config, never this one.
    constexpr const char* kEngineMarker = "Assets/Config/HardwareProfiles.json";


    std::string Generic( const fs::path& InPath )
    {
        std::string path = InPath.generic_string();
        while( path.size() > 1 && path.back() == '/' )
        {
            path.pop_back();
        }
        return path;
    }


    bool StartsWithFolder( const std::string& InPath, const std::string& InFolder )
    {
        return !InFolder.empty() && InPath.size() > InFolder.size() && InPath.compare( 0, InFolder.size(), InFolder ) == 0 && InPath[InFolder.size()] == '/';
    }
}


bool Project::InitFromCommandLine()
{
#if USING( ME_PLATFORM_UWP )
    // Packaged apps run from their install folder; Path keeps its UWP resolver.
    return true;
#else
    std::error_code error;
    std::string root = Generic( fs::current_path( error ) );
    if( CommandLine::Has( "--project" ) )
    {
        const std::string requested = CommandLine::GetString( "--project" );
        root = FindProjectRoot( requested );
        if( root.empty() )
        {
            std::fprintf( stderr, "--project: '%s' is not a project (a folder with %s)\n", requested.c_str(), kProjectFile );
            return false;
        }
        fs::current_path( root, error );
        if( error )
        {
            std::fprintf( stderr, "--project: can't change into '%s': %s\n", root.c_str(), error.message().c_str() );
            return false;
        }
    }

    std::string engine = root + "/Engine";
    if( !IsEngineDirectory( engine ) && IsEngineCheckout( root ) )
    {
        // Started in an engine checkout's own root (its unit tests, a debugger's default working
        // directory): the checkout is its own engine.
        engine = root;
    }
    else if( !IsEngineDirectory( engine ) && fs::is_regular_file( root + "/" + kProjectFile, error ) )
    {
        const std::string found = FindExecutableEngine();
        if( !found.empty() )
        {
            engine = found;
        }
    }
    VFS::Get().SetDefaultRoots( root, engine );
    return true;
#endif
}


std::string Project::GetRoot()
{
    return VFS::Get().GetGameRoot();
}


std::string Project::GetEngineRoot()
{
    return VFS::Get().GetEngineRoot();
}


bool Project::HasEmbeddedEngine()
{
    return GetEngineRoot() == GetRoot() + "/Engine";
}


std::string Project::GetName()
{
    std::string text;
    if( VFS::Get().ReadText( kProjectFile, text ) )
    {
        const json project = json::parse( text, nullptr, false );
        if( project.is_object() && project.contains( "ProjectName" ) && project["ProjectName"].is_string() && !project["ProjectName"].get<std::string>().empty() )
        {
            return project["ProjectName"].get<std::string>();
        }
    }
    return fs::path( GetRoot() ).filename().generic_string();
}


bool Project::HasGameModule()
{
    std::error_code error;
    return fs::is_regular_file( GetRoot() + "/Game.sharpmake.cs", error );
}


bool Project::IsExecutableInProject()
{
    return StartsWithFolder( GetExecutableDirectory(), GetRoot() );
}


std::string Project::GetExecutableDirectory()
{
    std::string directory;
    if( char* base = SDL_GetBasePath() )
    {
        directory = base;
        SDL_free( base );
    }
    std::replace( directory.begin(), directory.end(), '\\', '/' );
    if( !directory.empty() && directory.back() != '/' )
    {
        directory += '/';
    }
    return directory;
}


std::string Project::FindProjectRoot( const std::string& InFolderOrFile )
{
    if( InFolderOrFile.empty() )
    {
        return {};
    }
    std::error_code error;
    fs::path path = fs::absolute( InFolderOrFile, error ).lexically_normal();
    if( !path.has_filename() )
    {
        path = path.parent_path();
    }
    if( fs::is_regular_file( path, error ) && path.extension() == ".meproj" )
    {
        path = path.parent_path();
        if( path.filename() == "Project" )
        {
            path = path.parent_path();
        }
    }
    if( !fs::is_regular_file( path / kProjectFile, error ) )
    {
        return {};
    }
    return Generic( path );
}


bool Project::IsEngineDirectory( const std::string& InDirectory )
{
    std::error_code error;
    return !InDirectory.empty() && fs::is_regular_file( fs::path( InDirectory ) / kEngineMarker, error );
}


bool Project::IsEngineCheckout( const std::string& InDirectory )
{
    std::error_code error;
    return IsEngineDirectory( InDirectory ) && fs::is_regular_file( fs::path( InDirectory ) / "Engine.sharpmake.cs", error );
}


std::string Project::FindExecutableEngine()
{
    const std::string executable = GetExecutableDirectory();
    if( executable.empty() )
    {
        return {};
    }
    std::error_code error;
    fs::path directory = fs::weakly_canonical( fs::path( executable ), error );
    if( !directory.has_filename() )
    {
        directory = directory.parent_path();
    }
    while( !directory.empty() )
    {
        // A game checkout's (or an install's) Engine/ first, then an engine checkout itself.
        for( const fs::path& candidate : { directory / "Engine", directory } )
        {
            if( IsEngineDirectory( candidate.generic_string() ) )
            {
                return Generic( candidate );
            }
        }
        const fs::path parent = directory.parent_path();
        if( parent == directory )
        {
            break;
        }
        directory = parent;
    }
    return {};
}
