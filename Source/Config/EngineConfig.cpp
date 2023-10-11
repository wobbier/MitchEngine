#include "PCH.h"
#include "EngineConfig.h"

EngineConfig::EngineConfig( const Path& inPath )
    : ConfigFile( inPath )
{
}

void EngineConfig::OnSave( json& outJson )
{
    json& window = outJson["Window"];
    window["Width"] = WindowSize.x;
    window["Height"] = WindowSize.y;
    window["X"] = WindowPosition.x;
    window["Y"] = WindowPosition.y;
}

void EngineConfig::OnLoadConfig( const json& inJson )
{
    if( inJson.contains( "Window" ) )
    {
        const json& WindowConfig = GetJsonObject( "Window" );

        int WindowWidth = WindowConfig["Width"];
        int WindowHeight = WindowConfig["Height"];

        int WindowX = 0;
        int WindowY = 0;

        if( WindowConfig.contains( "X" ) )
        {
            WindowX = WindowConfig["X"];
        }

        if( WindowConfig.contains( "Y" ) )
        {
            WindowY = WindowConfig["Y"];
        }

        WindowPosition = { WindowX, WindowY };
        WindowSize = { WindowWidth, WindowHeight };
    }
}