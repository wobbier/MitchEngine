#pragma once
#include "Config.h"
#include <stdio.h>

class EngineConfig
    : public ConfigFile
{
public:
    EngineConfig() = default;
    EngineConfig( const Path& inPath );

    virtual void OnSave( json& outJson ) final;

    virtual void OnLoadConfig( const json& inJson ) final;

    Vector2 WindowSize = { 1920.f, 720.f };
    Vector2 WindowPosition = { 0.f, 0.f };
};