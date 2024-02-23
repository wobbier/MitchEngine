#include "PCH.h"
#include "EngineConfig.h"

EngineConfig::EngineConfig( const Path& inPath )
    : ConfigFile( inPath )
    , WindowTitle("MitchEngine")
{
}

void EngineConfig::OnSave( json& outJson )
{
    json& window = outJson["Window"];
    window["Width"] = WindowSize.x;
    window["Height"] = WindowSize.y;
    window["X"] = WindowPosition.x;
    window["Y"] = WindowPosition.y;
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