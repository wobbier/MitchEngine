#include "PCH.h"
#include "AudioOcclusion.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "ECS/Entity.h"
#include <algorithm>
#include <vector>

namespace
{
    Entity* HierarchyRoot( Entity* InEntity )
    {
        Transform* transform = InEntity ? InEntity->TryGetComponent<Transform>() : nullptr;
        while( transform && transform->GetParentTransform() )
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
    const Entity* sourceRoot = HierarchyRoot( &InSourceEntity );
    const Entity* listenerRoot = HierarchyRoot( InListenerEntity );
    std::vector<const Entity*> obstacles;
    for( const RaycastHit& hit : InPhysics.RaycastAll( InListener, delta, distance ) )
    {
        const Entity* root = HierarchyRoot( hit.Entity.Get() );
        if( root && root != sourceRoot && root != listenerRoot && std::find( obstacles.begin(), obstacles.end(), root ) == obstacles.end() )
        {
            obstacles.push_back( root );
        }
    }
    return static_cast<int>( obstacles.size() );
}
