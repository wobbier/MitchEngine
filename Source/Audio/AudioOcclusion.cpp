#include "PCH.h"
#include "AudioOcclusion.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "Cores/SceneCore.h"
#include "ECS/Entity.h"
#include <algorithm>
#include <vector>

namespace
{
    // The top of the entity's own object: its ancestor just below the scene root (SceneCore's
    // RootTransform, which every loaded entity hangs off), or the topmost one without a scene root.
    Entity* HierarchyRoot( Entity* InEntity, const Transform* InSceneRoot )
    {
        Transform* transform = InEntity ? InEntity->TryGetComponent<Transform>() : nullptr;
        while( transform && transform->GetParentTransform() && transform->GetParentTransform() != InSceneRoot )
        {
            transform = transform->GetParentTransform();
        }
        return transform && transform->Parent ? transform->Parent.Get() : InEntity;
    }
}


int CountAudioObstacles( PhysicsCore& InPhysics, const Vector3& InListener, const Vector3& InSource, Entity* InListenerEntity, Entity& InSourceEntity )
{
    const Vector3 delta = InSource - InListener;
    const float distance = delta.Length();
    if( distance < 0.01f )
    {
        return 0;
    }
    SceneCore* scene = InPhysics.GetWorld().GetCore<SceneCore>();
    const Transform* sceneRoot = scene ? scene->GetRootTransform() : nullptr;
    const Entity* sourceRoot = HierarchyRoot( &InSourceEntity, sceneRoot );
    const Entity* listenerRoot = HierarchyRoot( InListenerEntity, sceneRoot );
    std::vector<const Entity*> obstacles;
    for( const RaycastHit& hit : InPhysics.RaycastAll( InListener, delta, distance ) )
    {
        const Entity* root = HierarchyRoot( hit.Entity.Get(), sceneRoot );
        if( root && root != sourceRoot && root != listenerRoot && std::find( obstacles.begin(), obstacles.end(), root ) == obstacles.end() )
        {
            obstacles.push_back( root );
        }
    }
    return static_cast<int>( obstacles.size() );
}
