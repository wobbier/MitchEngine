#include "PCH.h"
#include "TerrainCore.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Core/UpdateContext.h"
#include <cmath>

TerrainCore::TerrainCore() : Base( ComponentFilter().Requires<Transform>().Requires<Terrain>() )
{
    SetIsSerializable( false );
    RunsBefore( "PhysicsCore" ); RunsBefore( "ScriptCore" );
}

void TerrainCore::SyncNow( bool InWaitForCollision, float InPhysicsStepSeconds )
{
    OPTICK_EVENT( "TerrainCore::SyncNow" );
    if( std::isfinite( InPhysicsStepSeconds ) && InPhysicsStepSeconds > 0 ) m_physicsStepSeconds = InPhysicsStepSeconds;
    m_collisionReady = true;
    for( Entity& entity : GetEntities() )
    {
        Terrain& terrain = entity.GetComponent<Terrain>();
        terrain.Resolve( InWaitForCollision && terrain.Collision );
        Transform& transform = entity.GetComponent<Transform>();
        if( !TerrainData::IsTranslationOnly( transform.GetLocalToWorldMatrix() ) )
        {
            terrain.m_valid = false;
            terrain.m_error = "Terrain supports world translation only; remove rotation and scale, including parent transforms";
        }
        for( Transform* node = &transform; node; node = node->GetParentTransform() )
            if( node->Parent )
                if( auto body = node->Parent->TryGetComponent<Rigidbody>(); body && body->IsEnabled() && body->Type != BodyType::Static )
                {
                    terrain.m_valid = false;
                    terrain.m_error = "Terrain is static and cannot belong to a dynamic or kinematic Rigidbody";
                    break;
                }
        if( entity.IsActiveInHierarchy() && terrain.IsEnabled() && terrain.Collision && !terrain.GetData() ) m_collisionReady = false;
    }
    SyncStreams();
}


void TerrainCore::Update( const UpdateContext& context )
{
    m_frame = context.GetFrameID();
    SyncNow( false, context.GetFixedDeltaTime() );
}
