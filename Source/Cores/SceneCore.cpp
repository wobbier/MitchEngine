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
        RootTransformEntity = GameWorld->CreateEntity();
        RootTransform = &RootTransformEntity->AddComponent<Transform>();
    }
}

void SceneCore::OnEntityAdded( Entity& NewEntity )
{
    Base::OnEntityAdded( NewEntity );

    Transform& NewEntityTransform = NewEntity.GetComponent<Transform>();

    if( !NewEntityTransform.ParentTransform && !( NewEntity.GetId() == RootTransformEntity->GetId() ) )
    {
        NewEntityTransform.SetParent( *GetRootTransform() );
    }
}

Transform* SceneCore::GetRootTransform()
{
    return RootTransform;
}

void SceneCore::OnEntityRemoved( Entity& InEntity )
{

}

void SceneCore::OnEntityDestroyed( Entity& InEntity )
{
    Transform& transform = InEntity.GetComponent<Transform>();
    if( transform.ParentTransform.get() )
    {
        transform.ParentTransform->RemoveChild( &transform );
    }
}

#if USING( ME_EDITOR )

void SceneCore::OnEditorInspect()
{
    BaseCore::OnEditorInspect();
}

#endif
