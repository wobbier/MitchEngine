#include "PCH.h"
#include "Tools/Projects/ProjectLibrary.h"

#include "CLog.h"
#include "Core/CommandLine.h"
#include "Engine/AutomationRunner.h"
#include "Engine/Project.h"
#include "JSON.h"
#include "SDL.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ProjectLibrary
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string Normalize( const std::string& InPath )
        {
            std::error_code error;
            std::string path = fs::absolute( InPath, error ).lexically_normal().generic_string();
            while( path.size() > 1 && path.back() == '/' )
            {
                path.pop_back();
            }
            return path;
        }


        std::string Lower( std::string InText )
        {
            std::transform( InText.begin(), InText.end(), InText.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return InText;
        }


        // Hub files live outside every project's mounts (other projects, the per-user list), so they
        // are read straight from disk rather than through the VFS.
        bool ReadJson( const std::string& InPath, json& OutJson )
        {
            std::ifstream stream( InPath, std::ios::binary );
            if( !stream )
            {
                return false;
            }
            std::stringstream text;
            text << stream.rdbuf();
            OutJson = json::parse( text.str(), nullptr, false );
            return !OutJson.is_discarded();
        }


        bool WriteJson( const std::string& InPath, const json& InJson )
        {
            std::error_code error;
            fs::create_directories( fs::path( InPath ).parent_path(), error );
            std::ofstream stream( InPath, std::ios::binary | std::ios::trunc );
            stream << InJson.dump( 4 ) << "\n";
            return static_cast<bool>( stream );
        }


        // Things a template folder may hold that are derived or per user, never content.
        bool IsDerivedFolder( const std::string& InName )
        {
            return InName == ".tmp" || InName == ".build" || InName == "Builds" || InName == "bin" || InName == "obj";
        }


        int64_t Now()
        {
            return std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch() ).count();
        }
    }


    std::vector<Template> ListTemplates( const std::string& InEngineRoot )
    {
        std::vector<Template> templates;
        std::error_code error;
        const fs::path folder = fs::path( InEngineRoot ) / "Templates";
        for( fs::directory_iterator it( folder, fs::directory_options::skip_permission_denied, error ), end; !error && it != end; it.increment( error ) )
        {
            json description;
            if( !it->is_directory( error ) || !ReadJson( ( it->path() / "Template.json" ).generic_string(), description ) || !description.is_object() )
            {
                continue;
            }
            Template entry;
            entry.Id = it->path().filename().generic_string();
            entry.Directory = Normalize( it->path().generic_string() );
            entry.Name = description.value( "Name", entry.Id );
            entry.Description = description.value( "Description", std::string() );
            entry.Order = description.value( "Order", 0 );
            if( Project::FindProjectRoot( entry.Directory ).empty() )
            {
                CLog::Log( CLog::LogType::Warning, "Project Hub: template " + entry.Directory + " has no " + Project::kProjectFile + "; skipped" );
                continue;
            }
            templates.push_back( std::move( entry ) );
        }
        std::sort( templates.begin(), templates.end(), []( const Template& InA, const Template& InB ) {
            return InA.Order != InB.Order ? InA.Order < InB.Order : InA.Name < InB.Name;
        } );
        return templates;
    }


    const Template* FindTemplate( const std::vector<Template>& InTemplates, const std::string& InIdOrName )
    {
        const std::string wanted = Lower( InIdOrName );
        for( const Template& entry : InTemplates )
        {
            if( Lower( entry.Id ) == wanted || Lower( entry.Name ) == wanted )
            {
                return &entry;
            }
        }
        return nullptr;
    }


    bool IsValidProjectName( const std::string& InName, std::string& OutError )
    {
        if( InName.empty() )
        {
            OutError = "Name the project";
            return false;
        }
        if( InName.size() > 64 )
        {
            OutError = "Project names are at most 64 characters";
            return false;
        }
        if( !std::isalnum( static_cast<unsigned char>( InName[0] ) ) )
        {
            OutError = "Project names start with a letter or digit";
            return false;
        }
        for( const char c : InName )
        {
            if( !std::isalnum( static_cast<unsigned char>( c ) ) && c != '-' && c != '_' )
            {
                OutError = std::string( "Project names use letters, digits, '-' and '_' (not '" ) + c + "')";
                return false;
            }
        }
        return true;
    }


    std::string CreateProject( const Template& InTemplate, const std::string& InParentFolder, const std::string& InName, std::string& OutError )
    {
        if( !IsValidProjectName( InName, OutError ) )
        {
            return {};
        }
        if( InParentFolder.empty() )
        {
            OutError = "Pick a location for the project";
            return {};
        }
        std::error_code error;
        const std::string root = Normalize( ( fs::path( InParentFolder ) / InName ).generic_string() );
        const bool existed = fs::exists( root, error );
        if( existed && ( !fs::is_directory( root, error ) || !fs::is_empty( root, error ) ) )
        {
            OutError = root + " already exists";
            return {};
        }
        // Keep a template from copying into itself.
        const std::string source = Normalize( InTemplate.Directory );
        if( root == source || root.rfind( source + "/", 0 ) == 0 )
        {
            OutError = "A project can't be created inside its template";
            return {};
        }

        auto fail = [&]( const std::string& InError ) {
            OutError = InError;
            std::error_code cleanup;
            if( existed )
            {
                for( fs::directory_iterator it( root, cleanup ), end; !cleanup && it != end; it.increment( cleanup ) )
                {
                    fs::remove_all( it->path(), cleanup );
                }
            }
            else
            {
                fs::remove_all( root, cleanup );
            }
            return std::string();
        };

        fs::create_directories( root, error );
        if( error )
        {
            return fail( "Can't create " + root + ": " + error.message() );
        }
        for( fs::recursive_directory_iterator it( source, fs::directory_options::skip_permission_denied, error ), end; !error && it != end; it.increment( error ) )
        {
            const fs::path relative = it->path().lexically_relative( source );
            const std::string name = it->path().filename().generic_string();
            if( it->is_directory( error ) )
            {
                if( IsDerivedFolder( name ) )
                {
                    it.disable_recursion_pending();
                    continue;
                }
                fs::create_directories( fs::path( root ) / relative, error );
            }
            else if( relative != "Template.json" )
            {
                fs::copy_file( it->path(), fs::path( root ) / relative, fs::copy_options::overwrite_existing, error );
            }
            if( error )
            {
                return fail( "Can't copy " + relative.generic_string() + ": " + error.message() );
            }
        }
        if( error )
        {
            return fail( "Can't read the template " + source + ": " + error.message() );
        }

        // Name it: the project, the product and the window title.
        json project;
        const std::string projectFile = root + "/" + Project::kProjectFile;
        if( !ReadJson( projectFile, project ) || !project.is_object() )
        {
            project = json::object();
        }
        project["ProjectName"] = InName;
        if( !WriteJson( projectFile, project ) )
        {
            return fail( "Can't write " + projectFile );
        }
        const std::string settingsFile = root + "/Assets/Config/ProjectSettings.json";
        json settings;
        if( ReadJson( settingsFile, settings ) && settings.is_object() )
        {
            if( !settings["Product"].is_object() )
            {
                settings["Product"] = json::object();
            }
            settings["Product"]["Name"] = InName;
            WriteJson( settingsFile, settings );
        }
        const std::string engineConfigFile = root + "/Assets/Config/Engine.cfg";
        json engineConfig;
        if( ReadJson( engineConfigFile, engineConfig ) && engineConfig.is_object() )
        {
            engineConfig["Title"] = InName;
            WriteJson( engineConfigFile, engineConfig );
        }
        CLog::Log( CLog::LogType::Info, "Project Hub: created " + root + " from the " + InTemplate.Name + " template" );
        return root;
    }


    std::string ReadProjectName( const std::string& InRoot )
    {
        json project;
        if( ReadJson( InRoot + "/" + Project::kProjectFile, project ) && project.is_object() )
        {
            const std::string name = project.value( "ProjectName", std::string() );
            if( !name.empty() )
            {
                return name;
            }
        }
        return fs::path( InRoot ).filename().generic_string();
    }


    RecentProjects::RecentProjects( std::string InFile )
        : m_file( std::move( InFile ) )
    {
    }


    std::string RecentProjects::GetDefaultFile()
    {
        const std::string file = CommandLine::GetString( "--recent-projects" );
        if( !file.empty() )
        {
            return Normalize( file );
        }
        if( AutomationRunner::IsUnattendedRun() )
        {
            return Normalize( ".tmp/Hub/RecentProjects.json" );
        }
        std::string folder;
        if( char* prefPath = SDL_GetPrefPath( "MitchEngine", "Havana" ) )
        {
            folder = prefPath;
            SDL_free( prefPath );
        }
        if( folder.empty() )
        {
            return Normalize( ".tmp/Hub/RecentProjects.json" );
        }
        return Normalize( folder + "/RecentProjects.json" );
    }


    void RecentProjects::Load()
    {
        m_entries.clear();
        json list;
        if( !ReadJson( m_file, list ) || !list.is_object() || !list["Projects"].is_array() )
        {
            return;
        }
        for( const json& item : list["Projects"] )
        {
            if( !item.is_object() || !item["Root"].is_string() )
            {
                continue;
            }
            RecentProject entry;
            entry.Root = Normalize( item["Root"].get<std::string>() );
            entry.Name = item.value( "Name", fs::path( entry.Root ).filename().generic_string() );
            entry.LastOpened = item.value( "LastOpened", int64_t( 0 ) );
            if( !entry.Root.empty() && !Contains( entry.Root ) && m_entries.size() < kMaxEntries )
            {
                m_entries.push_back( std::move( entry ) );
            }
        }
    }


    bool RecentProjects::Save() const
    {
        json projects = json::array();
        for( const RecentProject& entry : m_entries )
        {
            projects.push_back( { { "Root", entry.Root }, { "Name", entry.Name }, { "LastOpened", entry.LastOpened } } );
        }
        json list;
        list["Projects"] = projects;
        return WriteJson( m_file, list );
    }


    void RecentProjects::Add( const std::string& InRoot )
    {
        const std::string root = Normalize( InRoot );
        Remove( root );
        m_entries.insert( m_entries.begin(), { root, ReadProjectName( root ), Now() } );
        if( m_entries.size() > kMaxEntries )
        {
            m_entries.resize( kMaxEntries );
        }
    }


    bool RecentProjects::Remove( const std::string& InRoot )
    {
        const std::string root = Normalize( InRoot );
        const auto it = std::remove_if( m_entries.begin(), m_entries.end(), [&root]( const RecentProject& InEntry ) { return InEntry.Root == root; } );
        const bool removed = it != m_entries.end();
        m_entries.erase( it, m_entries.end() );
        return removed;
    }


    bool RecentProjects::Contains( const std::string& InRoot ) const
    {
        const std::string root = Normalize( InRoot );
        return std::any_of( m_entries.begin(), m_entries.end(), [&root]( const RecentProject& InEntry ) { return InEntry.Root == root; } );
    }
}
