#pragma once
#include "ECS/Core.h"
#include "ECS/CoreDetail.h"

// Engine-owned CPU publication/validation. Rendering and physics own their derived handles.
class TerrainCore final : public Core<TerrainCore>
{
public:
    TerrainCore();
    void Init() override {}
    void Update( const UpdateContext& ) override { SyncNow(); }
    void OnStart() override { SyncNow( true ); }
    // Waiting is an explicit resident-scene readiness barrier, used before physics starts.
    void SyncNow( bool InWaitForCollision = false );
    bool IsCollisionReady() const { return m_collisionReady; }
private:
    bool m_collisionReady = true;
};
ME_REGISTER_CORE( TerrainCore )
