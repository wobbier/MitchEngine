#pragma once
#include "ECS/Core.h"
#include "ECS/CoreDetail.h"
#include "Terrain/TerrainStreamScheduler.h"
#include <map>
#include <memory>
namespace TerrainData { struct StreamArchive; class TerrainStreamCache; }

// Engine-owned CPU publication/validation. Rendering and physics own their derived handles.
class TerrainCore final : public Core<TerrainCore>
{
public:
    TerrainCore();
    void Init() override {}
    void Update( const UpdateContext& InContext ) override;
    void OnStart() override;
    void OnStop() override;
    void OnEntityRemoved( Entity& InEntity ) override;
    void OnRemovedFromWorld() override { m_streams.clear(); m_scheduler = {}; }
    // Waiting is an explicit resident-scene readiness barrier, used before physics starts.
    void SyncNow( bool InWaitForCollision = false );
    bool IsCollisionReady() const { return m_collisionReady; }
    const TerrainData::StreamWorldStats& GetStreamingStats() const { return m_scheduler.GetStats(); }
private:
    bool m_collisionReady = true;
    uint64_t m_frame = 0;
    std::map<const TerrainData::StreamArchive*, std::shared_ptr<TerrainData::TerrainStreamCache>> m_streams;
    TerrainData::TerrainStreamScheduler m_scheduler;
    void SyncStreams();
};
ME_REGISTER_CORE( TerrainCore )
