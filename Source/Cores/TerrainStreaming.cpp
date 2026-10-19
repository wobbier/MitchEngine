#include "PCH.h"
#include "TerrainCore.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Physics/Colliders.h"
#include "Core/CommandLine.h"
#include "Components/Physics/CharacterController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/ProjectSettings.h"
#include "Resources/TerrainResource.h"
#include "Terrain/TerrainStreamCache.h"
#include "Utils/BGFXUtils.h"
#include <algorithm>
#include <cmath>
#include <set>

void TerrainCore::SyncStreams()
{
    struct Requests
    {
        TerrainData::StreamLimits Limits;
        std::map<TerrainData::TileCoord, TerrainData::TileDemand> Tiles;
        std::vector<Terrain*> Instances;
    };
    std::map<const TerrainData::StreamArchive*, Requests> groups;
    bool hasStreaming = false;
    for( Entity& entity : GetEntities() )
    {
        const auto& terrain = entity.GetComponent<Terrain>();
        if( entity.IsActiveInHierarchy() && terrain.IsEnabled() && terrain.m_valid && terrain.m_resource && terrain.m_resource->GetArchive() && terrain.m_data && terrain.m_data->Streamed ) { hasStreaming = true; break; }
    }
    if( !hasStreaming && m_streams.empty() )
    {
        for( Entity& entity : GetEntities() ) entity.GetComponent<Terrain>().m_stream.reset();
        m_scheduler = {}; return;
    }
    // Physics safety follows actors, including off-camera actors. Expansion includes body size
    // and predicted motion; the sample guard requests the neighboring tile at a shared seam.
    struct Actor { Vector3 Position; float Radius; };
    std::vector<Actor> actors;
    GetWorld().Each<Rigidbody, Transform>( [&]( Entity&, Rigidbody& body, Transform& transform ) {
        if( body.Type == BodyType::Static ) return;
        const auto velocity = body.GetVelocity();
        Actor actor{ transform.GetWorldPosition(), 2.f + std::hypot( velocity.x, velocity.z ) * 0.5f };
        if( body.m_hasPendingMove ) { const auto displacement = body.m_movePosition - actor.Position; actor.Radius += std::hypot( displacement.x, displacement.z ); }
        const auto visit = [&]( const auto& self, Transform& node ) -> void {
            if( node.Parent )
            {
                Entity& entity = *node.Parent.Get();
                if( &node != &transform ) if( auto* other = entity.TryGetComponent<Rigidbody>(); other && other->IsEnabled() ) return;
                const auto scale = node.GetWorldScale(); const float sizeScale = std::max( { std::abs( scale.x ), std::abs( scale.y ), std::abs( scale.z ) } );
                float radius = 0;
                if( auto* box = entity.TryGetComponent<BoxCollider>(); box && box->IsEnabled() ) radius = std::max( radius, ( box->Size.Length() * 0.5f + box->Center.Length() ) * sizeScale );
                if( auto* sphere = entity.TryGetComponent<SphereCollider>(); sphere && sphere->IsEnabled() ) radius = std::max( radius, ( sphere->Radius + sphere->Center.Length() ) * sizeScale );
                if( auto* capsule = entity.TryGetComponent<CapsuleCollider>(); capsule && capsule->IsEnabled() ) radius = std::max( radius, ( capsule->Radius + capsule->Height * 0.5f + capsule->Center.Length() ) * sizeScale );
                if( auto* mesh = entity.TryGetComponent<MeshCollider>(); mesh && mesh->IsEnabled() ) { AABB bounds; if( ColliderUtils::GetMeshBounds( entity, bounds ) ) radius = std::max( radius, ( bounds.Min.Length() + bounds.Max.Length() + mesh->Center.Length() ) * sizeScale ); }
                actor.Radius = std::max( actor.Radius, radius + ( node.GetWorldPosition() - actor.Position ).Length() + 2.f );
            }
            for( auto* child : node.GetChildren() ) self( self, *child );
        };
        visit( visit, transform ); actors.push_back( actor );
    } );
    GetWorld().Each<CharacterController, Transform>( [&]( Entity&, CharacterController& controller, Transform& transform ) {
        actors.push_back( { transform.GetWorldPosition() + controller.Center, controller.Radius + controller.MaxSpeed * 0.5f + std::hypot( controller.m_pendingMove.x, controller.m_pendingMove.z ) } );
    } );
    std::vector<Vector3> cameras;
    GetWorld().Each<Camera, Transform>( [&]( Entity&, Camera& camera, Transform& transform ) {
        if( camera.OutputSize.x > 0 && camera.OutputSize.y > 0 ) cameras.push_back( transform.GetWorldPosition() );
    } );
#if USING( ME_EDITOR )
    if( Moonlight::IsGpuAlive() && GetEngine().IsInitialized() && GetEngine().EditorCamera.ShouldRender ) cameras.push_back( GetEngine().EditorCamera.Position );
#endif
    for( Entity& entity : GetEntities() )
    {
        auto& terrain = entity.GetComponent<Terrain>();
        if( !entity.IsActiveInHierarchy() || !terrain.IsEnabled() || !terrain.m_valid || !terrain.m_resource || !terrain.m_resource->GetArchive() ||
            terrain.m_data == nullptr || !terrain.m_data->Streamed )
        {
            if( terrain.m_stream ) terrain.m_data = terrain.m_resource ? terrain.m_resource->GetData() : nullptr;
            terrain.m_stream.reset(); continue;
        }
        if( terrain.CpuBudgetMiB < 1 || terrain.GpuBudgetMiB < 1 || terrain.CollisionBudgetMiB < 1 || terrain.CpuBudgetMiB > 4096 || terrain.GpuBudgetMiB > 4096 || terrain.CollisionBudgetMiB > 4096 ||
            !std::isfinite( terrain.StreamDistance ) || terrain.StreamDistance < 0 || terrain.StreamDistance > 16384 || !std::isfinite( terrain.CollisionDistance ) || terrain.CollisionDistance < 1 || terrain.CollisionDistance > 1024 )
        {
            terrain.m_data = terrain.m_resource->GetData(); terrain.m_stream.reset();
            terrain.m_valid = false; terrain.m_error = "Invalid terrain streaming budgets or demand radius";
            if( terrain.Collision ) m_collisionReady = false; continue;
        }
        const auto archive = terrain.m_resource->GetArchive();
        auto& cache = m_streams[archive.get()]; if( !cache ) cache = std::make_shared<TerrainData::TerrainStreamCache>( archive ); terrain.m_stream = cache;
        auto& request = groups[archive.get()]; request.Instances.push_back( &terrain );
        if( request.Instances.size() == 1 )
        { request.Limits.CpuBytes = uint64_t( terrain.CpuBudgetMiB ) << 20; request.Limits.GpuBytes = uint64_t( terrain.GpuBudgetMiB ) << 20; request.Limits.CollisionBytes = uint64_t( terrain.CollisionBudgetMiB ) << 20; }
        request.Limits.CpuBytes = std::min( request.Limits.CpuBytes, uint64_t( terrain.CpuBudgetMiB ) << 20 );
        request.Limits.GpuBytes = std::min( request.Limits.GpuBytes, uint64_t( terrain.GpuBudgetMiB ) << 20 );
        request.Limits.CollisionBytes = std::min( request.Limits.CollisionBytes, uint64_t( terrain.CollisionBudgetMiB ) << 20 );
        const auto origin = entity.GetComponent<Transform>().GetWorldPosition();
        if( terrain.StreamDetail ) for( const auto& position : cameras ) TerrainData::AddStreamDemand( archive->Config, position - origin, terrain.StreamDistance, false, request.Tiles );
        if( terrain.Collision ) for( const auto& actor : actors ) TerrainData::AddStreamDemand( archive->Config, actor.Position - origin, terrain.CollisionDistance + actor.Radius + archive->Config.SampleSpacing, true, request.Tiles );
    }
    std::vector<TerrainData::StreamBatch> batches;
    size_t drainLimit = 1;
    for( auto& [archive, cache] : m_streams )
    {
        TerrainData::StreamBatch batch; batch.Cache = cache.get(); batch.Active = groups.contains( archive );
        if( batch.Active )
        {
            auto& requests = groups.at( archive ); batch.Limits = requests.Limits;
            batch.Limits.Instances = static_cast<uint32_t>( requests.Instances.size() );
            for( const auto& [coord, tile] : requests.Tiles ) batch.Demand.push_back( tile );
        }
        batches.push_back( std::move( batch ) ); drainLimit += archive->Blocks.size();
    }
    const auto& settings = ProjectSettings::Get();
    TerrainData::StreamWorldLimits limits;
    limits.CpuBytes = uint64_t( std::clamp( settings.TerrainCpuBudgetMiB, 1, 4096 ) ) << 20;
    limits.GpuBytes = uint64_t( std::clamp( settings.TerrainGpuBudgetMiB, 1, 4096 ) ) << 20;
    limits.CollisionBytes = uint64_t( std::clamp( settings.TerrainCollisionBudgetMiB, 1, 4096 ) ) << 20;
    limits.MaxLoads = static_cast<uint32_t>( std::clamp( settings.TerrainMaxLoads, 1, 64 ) );
    m_scheduler.Tick( m_frame, batches, limits );
    if( CommandLine::GetFloat( "--frame-time", 0 ) > 0 )
        for( size_t step = 0; m_scheduler.GetStats().Loading && step < drainLimit; ++step )
        {
            for( auto& batch : batches ) batch.Cache->WaitForRequest();
            m_scheduler.Tick( m_frame, batches, limits );
        }
    for( auto& [archive, requests] : groups )
    {
        auto& cache = m_streams.at( archive );
        const auto& stats = cache->GetStats();
        for( auto* terrain : requests.Instances )
        {
            if( !terrain->m_valid ) continue;
            terrain->m_data = cache->GetData();
            if( terrain->Collision && !stats.CollisionReady ) m_collisionReady = false;
        }
    }
    // Removed instances still have draw/query/physics consumers until their normal sync points.
    // Keep their reservations in the world allowance until those pins and owned jobs retire.
    for( auto it = m_streams.begin(); it != m_streams.end(); )
        if( !groups.contains( it->first ) && it->second->CanRetire() ) it = m_streams.erase( it ); else ++it;
}


void TerrainCore::OnStart()
{
    for( auto& [archive, cache] : m_streams ) cache->InvalidatePending();
    SyncNow( true );
}


void TerrainCore::OnStop()
{
    for( auto& [archive, cache] : m_streams ) cache->InvalidatePending();
}


void TerrainCore::OnEntityRemoved( Entity& entity )
{
    if( auto* terrain = entity.TryGetComponent<Terrain>(); terrain && terrain->m_stream )
    {
        terrain->m_data = terrain->m_resource ? terrain->m_resource->GetData() : nullptr;
        terrain->m_stream.reset();
    }
}
