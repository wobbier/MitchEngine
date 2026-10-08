#pragma once
#include <string>
#include "Path.h"
#include "Components/Transform.h"
#include "File.h"
#include "Engine/World.h"

// A scene file on disk. Loading/saving goes through SceneSerializer (format v2, v1 migrated on load).
class Scene
{
public:
    Scene( const std::string& SceneFilePath );

    void UnLoad();

    bool Load( SharedPtr<World> InWorld );

    bool IsNewScene();

    void Save( const std::string& fileName, Transform* root );
    // Writes the scene to fileName without changing this scene's FilePath (play-mode snapshots, autosave).
    void SaveCopy( const std::string& fileName, Transform* root );

    SharedPtr<World> GameWorld;
    File CurrentLevel;
    Path FilePath;
};
