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
}

void EngineConfig::OnLoadConfig( const json& inJson )
{
    if( inJson.contains( "Window" ) )
    {
        const json& WindowConfig = GetJsonObject( "Window" );

        int WindowWidth = WindowConfig["Width"];
        int WindowHeight = WindowConfig["Height"];

        WindowSize = { WindowWidth, WindowHeight };
    }
}