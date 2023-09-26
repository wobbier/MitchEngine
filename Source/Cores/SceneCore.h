#pragma once
#include "ECS/Core.h"

class Transform;

class SceneCore
    : public Core<SceneCore>
{
public:
    SceneCore();
    ~SceneCore();

    // Separate init from construction code.
    void Init() final;

    void OnEntityAdded( Entity& NewEntity ) final;
    void OnEntityRemoved( Entity& InEntity ) final;
    void OnEntityDestroyed( Entity& InEntity ) final;

    EntityHandle RootTransformEntity;
    Transform* GetRootTransform();

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

private:

    Transform* RootTransform = nullptr;

};