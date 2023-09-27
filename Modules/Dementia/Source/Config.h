#pragma once
#include <Path.h>
#include <File.h>
#include <JSON.h>

class ConfigFile
{
public:
    ConfigFile() = default;
    explicit ConfigFile( const Path& ConfigPath );
    virtual ~ConfigFile();

    std::string GetValue( const std::string& value );
    const json& GetJsonObject( const std::string& value );

    void SetValue( const std::string& key, const std::string& newVal );

    void Save();
    void Load();

    virtual void OnSave( json& outJson ) {};
    virtual void OnLoadConfig( const json& outJson ) {};

    json Root;

protected:
    File m_configFile;
};
