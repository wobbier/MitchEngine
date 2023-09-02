#pragma once
#include <Path.h>
#include <File.h>
#include <JSON.h>

class Config
{
public:
    Config() = default;
    explicit Config( const Path& ConfigPath );
    virtual ~Config();

    std::string GetValue( const std::string& value );
    const json& GetJsonObject( const std::string& value );

    void SetValue( const std::string& key, const std::string& newVal );

    void Save();

    virtual void OnSave( json& outJson ) {};
    virtual void OnLoad( const json& outJson ) {};

    json Root;

protected:
    File ConfigFile;
};
