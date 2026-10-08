#include "PCH.h"
#include "Cores/SceneCore.h"
#include "Components/Transform.h"
#include "Engine/World.h"

SceneCore::SceneCore()
    : Base( ComponentFilter().Requires<Transform>() )
{
    SetIsSerializable( false );
}

SceneCore::~SceneCore()
{
}

void SceneCore::Init()
{
    if( !RootTransformEntity )
    {
        RootTransformEntity = GameWorld->CreateEntity( "RootTransform" );
        RootTransform = &RootTransformEntity->AddComponent<Transform>();
    }
}

void SceneCore::OnEntityAdded( Entity& NewEntity )
{
    Base::OnEntityAdded( NewEntity );

    // Every transform without a parent hangs off the scene root.
    Transform& NewEntityTransform = NewEntity.GetComponent<Transform>();
    if( !NewEntityTransform.GetParentTransform() && NewEntity.GetId() != RootTransformEntity.GetID() && GetRootTransform() )
    {
        NewEntityTransform.SetParent( *GetRootTransform() );
    }
}

Transform* SceneCore::GetRootTransform()
{
    return RootTransformEntity ? RootTransform : nullptr;
}

void SceneCore::OnEntityRemoved( Entity& InEntity )
{
}

void SceneCore::OnEntityDestroyed( Entity& InEntity )
{
    // Transform::OnDestroy unlinks the hierarchy.
}

#if USING( ME_EDITOR )

void SceneCore::OnEditorInspect()
{
    BaseCore::OnEditorInspect();
}

#endif
