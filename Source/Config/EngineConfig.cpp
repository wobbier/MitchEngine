#include "PCH.h"
#include "EngineConfig.h"
#include "Engine/AutomationRunner.h"

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


const char* EngineConfig::DefaultUserStatePath()
{
#if USING( ME_EDITOR )
    return ".tmp/Engine.user.cfg";
#else
    return "user://Engine.cfg";
#endif
}

void EngineConfig::OnSave( json& outJson )
{
    json& window = outJson["Window"];
    window["Width"] = WindowSize.x;
    window["Height"] = WindowSize.y;
    window["X"] = Mathf::Min( 0.f, WindowPosition.x );
    window["Y"] = Mathf::Min( 0.f, WindowPosition.y );
    window["Title"] = WindowTitle;
    window["Mode"] = ToString( Mode );
    window["FullscreenWidth"] = FullscreenResolution.x;
    window["FullscreenHeight"] = FullscreenResolution.y;
    window["Display"] = Display;
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

        if( WindowConfig.contains( "Mode" ) && WindowConfig["Mode"].is_string() )
        {
            ParseWindowMode( WindowConfig["Mode"].get<std::string>(), Mode );
        }
        FullscreenResolution.x = WindowConfig.value( "FullscreenWidth", FullscreenResolution.x );
        FullscreenResolution.y = WindowConfig.value( "FullscreenHeight", FullscreenResolution.y );
        Display = WindowConfig.value( "Display", Display );
    }
}


void EngineConfig::LoadUserState()
{
#if !USING( ME_EDITOR )
    // Automated game runs stay deterministic: whatever the player's machine has saved under
    // user:// must not leak into captures. The editor keeps reading its repo-local state.
    if( AutomationRunner::IsUnattendedRun() )
    {
        return;
    }
#endif
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
    // File::Write creates missing directories through the VFS (user:// paths resolve there).
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
