#include "Config.h"

Config::Config( const Path& ConfigPath )
    : ConfigFile( ConfigPath )
{
    const std::string& configData = ConfigFile.Read();
    if ( configData.empty() )
    {
        YIKES( "[Config] Empty File: " + ConfigFile.FilePath.LocalPath );
        return;
    }

    Root = json::parse( configData );
    OnLoad( Root );
    if ( Root.is_null() )
    {
        YIKES( "Failed to parse config: " + ConfigFile.FilePath.LocalPath );
        return;
    }

}

Config::~Config()
{
}

std::string Config::GetValue( const std::string& value )
{
    if ( Root.contains( value ) )
    {
        return Root[value];
    }
    return "";
}

const json& Config::GetJsonObject( const std::string& value )
{
    if ( Root.contains( value ) )
    {
        return Root[value];
    }
    return Root;
}

void Config::SetValue( const std::string& key, const std::string& newVal )
{
    Root[key] = newVal;
}

void Config::Save()
{
    OnSave( Root );
    ConfigFile.Write( Root.dump( 4 ) );
}
