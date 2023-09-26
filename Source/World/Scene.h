#pragma once
#include <string>
#include "Path.h"
#include "Components/Transform.h"
#include "File.h"
#include "Engine/World.h"

class Scene
{
public:
    Scene( const std::string& SceneFilePath );

    void UnLoad();

    void LoadSceneObject( const json& obj, Transform* parent );
    bool Load( SharedPtr<World> InWorld );
    void LoadCore( json& core );

    bool IsNewScene();

    void Save( const std::string& fileName, Transform* root );
    void SaveSceneRecursively( json& d, Transform* CurrentTransform );

    SharedPtr<World> GameWorld;
    File CurrentLevel;
    Path FilePath;
};