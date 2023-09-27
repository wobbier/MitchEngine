#include "Config.h"

ConfigFile::ConfigFile( const Path& ConfigPath )
    : m_configFile( ConfigPath )
{
    Load();
}

ConfigFile::~ConfigFile()
{
}

std::string ConfigFile::GetValue( const std::string& value )
{
    if( Root.contains( value ) )
    {
        return Root[value];
    }
    return "";
}

const json& ConfigFile::GetJsonObject( const std::string& value )
{
    if( Root.contains( value ) )
    {
        return Root[value];
    }
    return Root;
}

void ConfigFile::SetValue( const std::string& key, const std::string& newVal )
{
    Root[key] = newVal;
}

void ConfigFile::Save()
{
    OnSave( Root );
    m_configFile.Write( Root.dump( 4 ) );
}

void ConfigFile::Load()
{
    const std::string& configData = m_configFile.Read();
    if( configData.empty() )
    {
        YIKES( "[Config] Empty File: " + m_configFile.FilePath.GetLocalPathString() );
        return;
    }

    Root = json::parse( configData );

    if( Root.is_null() )
    {
        YIKES( "Failed to parse config: " + m_configFile.FilePath.GetLocalPathString() );
        return;
    }

    OnLoadConfig( Root );
}
