#include "PCH.h"
#include "EngineConfig.h"
#include "Engine/AutomationRunner.h"
#include <filesystem>

namespace
{
    // The Engine.cfg keys that belong to the user rather than the project.
    constexpr const char* kUserKeys[] = { "CurrentScene", "Window" };
}

EngineConfig::EngineConfig( const Path& inPath, const std::string& InUserStatePath )
    : ConfigFile( inPath )
    , WindowTitle( "MitchEngine" )
    , m_userStatePath( InUserStatePath )
{
}

void EngineConfig::OnSave( json& outJson )
{
    json& window = outJson["Window"];
    window["Width"] = WindowSize.x;
    window["Height"] = WindowSize.y;
    window["X"] = Mathf::Min( 0.f, WindowPosition.x );
    window["Y"] = Mathf::Min( 0.f, WindowPosition.y );
    window["Title"] = WindowTitle;
}

void EngineConfig::OnLoadConfig( const json& inJson )
{
    if( inJson.contains( "Window" ) )
    {
        const json& WindowConfig = GetJsonObject( "Window" );

        if( WindowConfig.contains( "X" ) )
        {
            WindowPosition.x = WindowConfig["X"];
        }

        if( WindowConfig.contains( "Y" ) )
        {
            WindowPosition.y = WindowConfig["Y"];
        }

        if( WindowConfig.contains( "Width" ) )
        {
            WindowSize.x = WindowConfig["Width"];
        }

        if( WindowConfig.contains( "Height" ) )
        {
            WindowSize.y = WindowConfig["Height"];
        }

        if( WindowConfig.contains( "Title" ) )
        {
            WindowTitle = WindowConfig["Title"];
        }
    }
}


void EngineConfig::LoadUserState()
{
    Path userPath( m_userStatePath, true );
    if( !userPath.Exists )
    {
        return;
    }
    File userFile( userPath );
    const json user = json::parse( userFile.Read(), nullptr, false );
    if( !user.is_object() )
    {
        return;
    }
    for( const char* key : kUserKeys )
    {
        if( user.contains( key ) )
        {
            Root[key] = user[key];
        }
    }
    OnLoadConfig( Root );
}


void EngineConfig::SaveUserState()
{
    if( AutomationRunner::IsUnattendedRun() )
    {
        return;
    }
    OnSave( Root );
    json user = json::object();
    for( const char* key : kUserKeys )
    {
        if( Root.contains( key ) )
        {
            user[key] = Root[key];
        }
    }
    std::error_code ec;
    std::filesystem::create_directories( std::filesystem::path( m_userStatePath ).parent_path(), ec );
    File userFile( Path( m_userStatePath, true ) );
    userFile.Write( user.dump( 4 ) );
}


void EngineConfig::SaveProjectValues( const json& InValues )
{
    json project = json::parse( m_configFile.Read(), nullptr, false );
    if( !project.is_object() )
    {
        project = json::object();
    }
    for( const auto& [key, value] : InValues.items() )
    {
        project[key] = value;
        Root[key] = value;
    }
    m_configFile.Write( project.dump( 4 ) );
}
