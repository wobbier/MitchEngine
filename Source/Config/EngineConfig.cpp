#include "PCH.h"
#include "EngineConfig.h"


void EngineConfig::OnLoad(const json& inJson)
{
    if (inJson.contains("Window"))
    {
        const json& WindowConfig = GetJsonObject("Window");

        int WindowWidth = WindowConfig["Width"];
        int WindowHeight = WindowConfig["Height"];

        WindowSize = { WindowWidth, WindowHeight };
    }
}