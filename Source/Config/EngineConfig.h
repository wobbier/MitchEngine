#pragma once
#include "Config.h"
#include <stdio.h>

class EngineConfig
    : public Config
{
public:
    EngineConfig( const Path& inPath );

    virtual void OnSave( json& outJson ) final;

    virtual void OnLoad( const json& inJson ) final;

    Vector2 WindowSize = { 1920.f, 720.f };
};