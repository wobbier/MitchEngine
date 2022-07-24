#pragma once
#include "Config.h"
#include <stdio.h>

class EngineConfig
    : public Config
{
public:
    EngineConfig(const Path& inPath)
        : Config(inPath)
    {
        std::cout << "llmfao";
    }

    virtual void OnSave(json& outJson) final
    {

    }

    virtual void OnLoad(const json& inJson) final;

    Vector2 WindowSize;
};