#include "PCH.h"
#include "Mods/Mods.h"
#include "Core/CommandLine.h"
#include "Engine/EngineVersion.h"
#include "Engine/Project.h"
#include "VFS/Mount.h"
#include "VFS/VFS.h"
#include <filesystem>
#include <fstream>
#include <sstream>


ModManager& ModManager::Get()
{
    static ModManager instance;
    return instance;
}


std::vector<ModManifest> ModManager::Discover( const std::vector<std::string>& InDirectories, std::vector<std::string>* OutProblems )
{
    namespace fs = std::filesystem;
    std::vector<ModManifest> manifests;
    for( const std::string& directory : InDirectories )
    {
        std::error_code error;
        if( directory.empty() || !fs::is_directory( directory, error ) )
        {
            continue;
        }
        // Subfolders in name order, so discovery (and which duplicate wins) is stable.
        std::vector<fs::path> folders;
        for( const fs::directory_entry& entry : fs::directory_iterator( directory, error ) )
        {
            if( entry.is_directory( error ) && fs::exists( entry.path() / "mod.json", error ) )
            {
                folders.push_back( entry.path() );
            }
        }
        std::sort( folders.begin(), folders.end() );
        for( const fs::path& folder : folders )
        {
            std::ifstream file( folder / "mod.json" );
            std::stringstream text;
            text << file.rdbuf();
            const json parsed = json::parse( text.str(), nullptr, false );
            ModManifest manifest;
            std::string problem;
            if( parsed.is_discarded() )
            {
                problem = "mod.json isn't valid JSON";
            }
            else if( !ModManifest::Parse( parsed, manifest, problem ) )
            {
                problem = "mod.json: " + problem;
            }
            if( !problem.empty() )
            {
                if( OutProblems )
                {
                    OutProblems->push_back( folder.generic_string() + ": " + problem );
                }
                continue;
            }
            manifest.Root = fs::absolute( folder, error ).lexically_normal().generic_string();
            manifests.push_back( std::move( manifest ) );
        }
    }
    return manifests;
}


ModSettings ModManager::LoadSettings() const
{
    std::string text;
    if( !VFS::Get().ReadText( std::string( kSettingsPath ), text ) )
    {
        return ModSettings();
    }
    return ModSettings::FromJson( json::parse( text, nullptr, false ) );
}


bool ModManager::SaveSettings( const ModSettings& InSettings ) const
{
    return VFS::Get().WriteText( std::string( kSettingsPath ), InSettings.ToJson().dump( 4 ) );
}


void ModManager::Apply( const std::string& InPrefix )
{
    std::vector<std::string> directories = { Project::GetRoot() + "/Mods", VFS::Get().GetUserRoot() + "/Mods" };
    if( CommandLine::Has( "--mod-dir" ) )
    {
        directories.insert( directories.begin(), CommandLine::GetString( "--mod-dir" ) );
    }
    m_problems.clear();
    m_discovered = Discover( directories, &m_problems );
    SemVer engine;
    SemVer::Parse( kEngineVersion, engine );
    const ModResolution resolution = ModResolver::Resolve( m_discovered, LoadSettings(), engine );
    m_problems.insert( m_problems.end(), resolution.Problems.begin(), resolution.Problems.end() );
    Mount( resolution, InPrefix );
    for( const std::string& problem : m_problems )
    {
        CLog::Log( CLog::LogType::Warning, "Mods: " + problem );
    }
    if( !m_loaded.empty() )
    {
        std::string list;
        for( const std::string& id : m_loaded )
        {
            list += ( list.empty() ? "" : ", " ) + id;
        }
        CLog::Log( CLog::LogType::Info, "Mods: loaded " + std::to_string( m_loaded.size() ) + " (" + list + ")" );
    }
}


void ModManager::Mount( const ModResolution& InResolution, const std::string& InPrefix )
{
    UnmountAll();
    int order = 0;
    for( const ModManifest* manifest : InResolution.Load )
    {
        // Read-only: writes keep going to the game (and user://), never into a mod's folder.
        auto source = std::make_shared<DirectoryMount>( manifest->Root );
        VFS::Get().Mount( InPrefix, source, kBasePriority + order++, false );
        m_mounts.push_back( source );
        m_loaded.push_back( manifest->Id );
    }
}


void ModManager::UnmountAll()
{
    for( const auto& source : m_mounts )
    {
        VFS::Get().Unmount( source );
    }
    m_mounts.clear();
    m_loaded.clear();
}
