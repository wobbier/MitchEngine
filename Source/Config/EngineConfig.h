#pragma once
#include "Config.h"
#include <stdio.h>

class EngineConfig
    : public ConfigFile
{
public:
    EngineConfig() = default;
    // InUserStatePath: where the per-user state lives (tests point it elsewhere).
    EngineConfig( const Path& inPath, const std::string& InUserStatePath = ".tmp/Engine.user.cfg" );

    virtual void OnSave( json& outJson ) final;

    virtual void OnLoadConfig( const json& inJson ) final;

    // Per-user state (the window placement and the scene the editor reopens) lives in
    // .tmp/Engine.user.cfg, layered over the project's Engine.cfg, so running the editor or the
    // game never rewrites a tracked file. Unattended runs read it but never write it.
    void LoadUserState();
    void SaveUserState();
    // Writes InValues into the project's Engine.cfg, keeping its other keys and none of the user state.
    void SaveProjectValues( const json& InValues );

    Vector2 WindowSize = { 1280.f, 720.f };
    Vector2 WindowPosition = { 0.f, 0.f };
    std::string WindowTitle;

private:
    std::string m_userStatePath = ".tmp/Engine.user.cfg";
};