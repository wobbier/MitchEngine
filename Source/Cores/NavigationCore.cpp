#include "PCH.h"
#include "NavigationCore.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Navigation/NavMeshAgent.h"
#include "Components/Navigation/NavMeshModifiers.h"
#include "Components/Navigation/NavMeshSurface.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Debug/DebugDraw.h"
#include "ECS/ComponentFilter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Graphics/MeshData.h"
#include "World/Scene.h"
#include "optick.h"

#include "DetourCrowd.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <glm/gtc/matrix_inverse.hpp>

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

namespace
{
    constexpr int kMaxCrowdAgents = 256;
    constexpr float kPi = 3.14159265f;

    template<typename T>
    T* Enabled( Entity& InEntity )
    {
        T* component = InEntity.TryGetComponent<T>();
        return component && component->IsEnabled() ? component : nullptr;
    }

    Vector3 TransformPoint( const glm::mat4& InMatrix, const Vector3& InPoint )
    {
        const glm::vec4 result = InMatrix * glm::vec4( InPoint.InternalVector, 1.f );
        return Vector3( result.x, result.y, result.z );
    }

    float DistanceXZ( const Vector3& InA, const Vector3& InB )
    {
        const float dx = InA.x - InB.x;
        const float dz = InA.z - InB.z;
        return std::sqrt( dx * dx + dz * dz );
    }

    // Collects triangles in world space, flipping winding for mirrored transforms.
    struct TriangleSink
    {
        NavBuildInput& Input;

        void Add( const glm::mat4& InMatrix, const std::vector<Vector3>& InPoints, const std::vector<uint32_t>& InIndices, uint8_t InArea )
        {
            const bool mirrored = glm::determinant( InMatrix ) < 0.f;
            const int base = static_cast<int>( Input.Vertices.size() );
            for( const Vector3& point : InPoints )
            {
                Input.Vertices.push_back( TransformPoint( InMatrix, point ) );
            }
            for( size_t i = 0; i + 2 < InIndices.size(); i += 3 )
            {
                const int a = base + static_cast<int>( InIndices[i] );
                const int b = base + static_cast<int>( InIndices[i + 1] );
                const int c = base + static_cast<int>( InIndices[i + 2] );
                Input.Indices.insert( Input.Indices.end(), { a, mirrored ? c : b, mirrored ? b : c } );
                Input.Areas.push_back( InArea );
            }
        }
    };

    // Unit shapes, counter-clockwise seen from outside (cross( b - a, c - a ) points out).
    void BoxGeometry( const Vector3& InCenter, const Vector3& InSize, std::vector<Vector3>& OutPoints, std::vector<uint32_t>& OutIndices )
    {
        const Vector3 h = InSize * 0.5f;
        for( int i = 0; i < 8; ++i )
        {
            OutPoints.push_back( InCenter + Vector3( ( i & 1 ) ? h.x : -h.x, ( i & 2 ) ? h.y : -h.y, ( i & 4 ) ? h.z : -h.z ) );
        }
        // Corner i: bit0 = +x, bit1 = +y, bit2 = +z.
        static const uint32_t kFaces[6][4] = {
            { 2, 6, 7, 3 },     // +y
            { 0, 1, 5, 4 },     // -y
            { 1, 3, 7, 5 },     // +x
            { 0, 4, 6, 2 },     // -x
            { 4, 5, 7, 6 },     // +z
            { 0, 2, 3, 1 },     // -z
        };
        for( const auto& face : kFaces )
        {
            OutIndices.insert( OutIndices.end(), { face[0], face[1], face[2], face[0], face[2], face[3] } );
        }
    }

    // Latitude / longitude capsule along +Y (a sphere when InHalfLength is 0).
    void CapsuleGeometry( const Vector3& InCenter, float InRadius, float InHalfLength, const glm::mat3& InAxisFrame, std::vector<Vector3>& OutPoints, std::vector<uint32_t>& OutIndices )
    {
        constexpr int kSlices = 12;
        constexpr int kStacks = 8;   // per hemisphere pair, even
        for( int stack = 0; stack <= kStacks; ++stack )
        {
            const float phi = kPi * static_cast<float>( stack ) / kStacks;   // 0 = top
            const float y = std::cos( phi ) * InRadius + ( stack <= kStacks / 2 ? InHalfLength : -InHalfLength );
            const float ring = std::sin( phi ) * InRadius;
            for( int slice = 0; slice < kSlices; ++slice )
            {
                const float theta = 2.f * kPi * static_cast<float>( slice ) / kSlices;
                const glm::vec3 local( std::cos( theta ) * ring, y, std::sin( theta ) * ring );
                OutPoints.push_back( InCenter + Vector3( InAxisFrame * local ) );
            }
        }
        for( int stack = 0; stack < kStacks; ++stack )
        {
            for( int slice = 0; slice < kSlices; ++slice )
            {
                const uint32_t a = stack * kSlices + slice;
                const uint32_t b = stack * kSlices + ( slice + 1 ) % kSlices;
                const uint32_t c = ( stack + 1 ) * kSlices + slice;
                const uint32_t d = ( stack + 1 ) * kSlices + ( slice + 1 ) % kSlices;
                OutIndices.insert( OutIndices.end(), { a, b, c, b, d, c } );
            }
        }
    }

    // Convex hull of points on the XZ plane (monotone chain).
    std::vector<Vector3> HullXZ( std::vector<Vector3> InPoints )
    {
        std::sort( InPoints.begin(), InPoints.end(), []( const Vector3& a, const Vector3& b ) { return a.x < b.x || ( a.x == b.x && a.z < b.z ); } );
        auto cross = []( const Vector3& o, const Vector3& a, const Vector3& b ) { return ( a.x - o.x ) * ( b.z - o.z ) - ( a.z - o.z ) * ( b.x - o.x ); };
        std::vector<Vector3> hull( InPoints.size() * 2 );
        size_t k = 0;
        for( size_t i = 0; i < InPoints.size(); ++i )
        {
            while( k >= 2 && cross( hull[k - 2], hull[k - 1], InPoints[i] ) <= 0.f )
            {
                --k;
            }
            hull[k++] = InPoints[i];
        }
        for( size_t i = InPoints.size() - 1, t = k + 1; i > 0; --i )
        {
            while( k >= t && cross( hull[k - 2], hull[k - 1], InPoints[i - 1] ) <= 0.f )
            {
                --k;
            }
            hull[k++] = InPoints[i - 1];
        }
        hull.resize( k > 1 ? k - 1 : k );
        return hull;
    }

    Vector4 AreaColor( uint8_t InArea, float InAlpha )
    {
        switch( InArea )
        {
        case NavAreas::Walkable: return Vector4( 0.15f, 0.55f, 1.f, InAlpha );
        case NavAreas::Jump: return Vector4( 1.f, 0.55f, 0.1f, InAlpha );
        default:
        {
            // Spread the other areas around the hue wheel.
            const float hue = std::fmod( InArea * 0.61803f, 1.f ) * 6.f;
            const float x = 1.f - std::fabs( std::fmod( hue, 2.f ) - 1.f );
            const int sector = static_cast<int>( hue );
            const float r = sector == 0 || sector == 5 ? 1.f : ( sector == 1 || sector == 4 ? x : 0.f );
            const float g = sector == 1 || sector == 2 ? 1.f : ( sector == 0 || sector == 3 ? x : 0.f );
            const float b = sector == 3 || sector == 4 ? 1.f : ( sector == 2 || sector == 5 ? x : 0.f );
            return Vector4( r * 0.8f + 0.1f, g * 0.8f + 0.1f, b * 0.8f + 0.1f, InAlpha );
        }
        }
    }

    std::string SanitizeFileName( const std::string& InName )
    {
        std::string name = InName.empty() ? "NavMesh" : InName;
        for( char& c : name )
        {
            if( !std::isalnum( static_cast<unsigned char>( c ) ) && c != '-' && c != '_' )
            {
                c = '_';
            }
        }
        return name;
    }
}


NavigationCore::NavigationCore()
    : Base( ComponentFilter().Requires<Transform>().RequiresOneOf<NavMeshSurface>().RequiresOneOf<NavMeshAgent>() )
{
    SetIsSerializable( false );
}


NavigationCore::~NavigationCore()
{
    OnRemovedFromWorld();
}


void NavigationCore::Init()
{
    // A new World (scene load): nothing carries over.
    OnRemovedFromWorld();
}


void NavigationCore::OnRemovedFromWorld()
{
    for( auto& [id, record] : m_surfaces )
    {
        CancelBake( record );
        if( record.Carve )
        {
            record.Carve->Cancel = true;
            if( record.Carve->Thread.joinable() )
            {
                record.Carve->Thread.join();
            }
        }
        DestroyCrowd( record );
    }
    m_surfaces.clear();
    m_agents.clear();
    m_obstacles.clear();
}


void NavigationCore::SyncNow()
{
    PollBakes();
    PollCarves();
    SyncSurfaces();
}


void NavigationCore::OnStart()
{
    m_running = true;
    SyncNow();
    // Agents join their crowds from where they stand now.
    for( auto& [id, agent] : m_agents )
    {
        RemoveAgent( agent );
    }
    m_agents.clear();
}


void NavigationCore::OnStop()
{
    m_running = false;
    for( auto& [id, agent] : m_agents )
    {
        RemoveAgent( agent );
    }
    m_agents.clear();
    // Obstacles carved the navmesh only for this run.
    RestoreBakedMeshes();
    m_obstacles.clear();
}


void NavigationCore::OnEntityRemoved( Entity& InEntity )
{
    const uint64_t id = InEntity.GetId().Value();
    auto agent = m_agents.find( id );
    if( agent != m_agents.end() )
    {
        RemoveAgent( agent->second );
        m_agents.erase( agent );
    }
    auto surface = m_surfaces.find( id );
    if( surface != m_surfaces.end() && !InEntity.TryGetComponent<NavMeshSurface>() )
    {
        CancelBake( surface->second );
        DestroyCrowd( surface->second );
        for( auto& [agentId, record] : m_agents )
        {
            if( record.Surface == id )
            {
                record.CrowdIndex = -1;
                record.Surface = 0;
            }
        }
        m_surfaces.erase( surface );
    }
}


void NavigationCore::Update( const UpdateContext& inUpdateContext )
{
    OPTICK_EVENT( "NavigationCore::Update" );
    SyncNow();
    if( m_running )
    {
        SyncAgents();
        const float deltaSeconds = inUpdateContext.GetDeltaTime();
        if( deltaSeconds > 0.f )
        {
            StepAgents( deltaSeconds );
        }
        SyncObstacles( deltaSeconds );
        StartCarves();
    }
    DrawDebug();
}


// ---------------------------------------------------------------------------------------- Surfaces

std::string NavigationCore::GetDataPath( const NavMeshSurface& InSurface ) const
{
    if( !InSurface.NavMeshData.empty() )
    {
        return InSurface.NavMeshData;
    }
    const Scene* scene = GetEngine().CurrentScene;
    const std::string scenePath = scene ? scene->FilePath.GetLocalPathString() : std::string();
    if( scenePath.empty() )
    {
        return std::string();
    }
    const size_t dot = scenePath.find_last_of( '.' );
    const std::string stem = dot == std::string::npos ? scenePath : scenePath.substr( 0, dot );
    const std::string name = InSurface.Parent ? InSurface.Parent->GetName() : std::string();
    return stem + "." + SanitizeFileName( name ) + ".navmesh";
}


NavigationCore::SurfaceRecord* NavigationCore::FindSurface( const NavMeshSurface& InSurface )
{
    if( !InSurface.Parent )
    {
        return nullptr;
    }
    auto it = m_surfaces.find( InSurface.Parent->GetId().Value() );
    return it != m_surfaces.end() ? &it->second : nullptr;
}


const NavigationCore::SurfaceRecord* NavigationCore::FindSurface( const NavMeshSurface& InSurface ) const
{
    if( !InSurface.Parent )
    {
        return nullptr;
    }
    auto it = m_surfaces.find( InSurface.Parent->GetId().Value() );
    return it != m_surfaces.end() ? &it->second : nullptr;
}


void NavigationCore::SyncSurfaces()
{
    for( Entity& entity : GetEntities() )
    {
        NavMeshSurface* surface = entity.TryGetComponent<NavMeshSurface>();
        if( !surface )
        {
            continue;
        }
        SurfaceRecord& record = m_surfaces[entity.GetId().Value()];
        record.Surface = entity.GetHandle();
        LoadSurface( record, *surface );
    }
}


void NavigationCore::LoadSurface( SurfaceRecord& InRecord, NavMeshSurface& InSurface )
{
    if( InRecord.Bake )
    {
        return;
    }
    const std::string path = GetDataPath( InSurface );
    if( InRecord.LoadAttempted && path == InRecord.LoadedPath )
    {
        return;
    }
    InRecord.LoadAttempted = true;
    InRecord.LoadedPath = path;
    if( !path.empty() )
    {
        const Path file( path );
        NavMeshData data;
        if( file.Exists && data.Load( file.FullPath ) )
        {
            SetMesh( InRecord, data );
            return;
        }
    }
    if( InSurface.BakeOnLoad )
    {
        Bake( InSurface, true );
    }
}


void NavigationCore::SetMesh( SurfaceRecord& InRecord, const NavMeshData& InData )
{
    DestroyCrowd( InRecord );
    if( !InRecord.Mesh )
    {
        InRecord.Mesh = std::make_unique<NavMesh>();
    }
    if( !InRecord.Mesh->Load( InData ) )
    {
        InRecord.LastError = "the navmesh data is invalid";
        InRecord.Mesh.reset();
    }
}


void NavigationCore::DestroyCrowd( SurfaceRecord& InRecord )
{
    if( !InRecord.Crowd )
    {
        return;
    }
    dtFreeCrowd( InRecord.Crowd );
    InRecord.Crowd = nullptr;
    InRecord.CrowdRadius = 0.f;
    InRecord.FilterMasks.clear();
    const uint64_t surfaceId = InRecord.Surface ? InRecord.Surface->GetId().Value() : 0;
    for( auto& [id, agent] : m_agents )
    {
        if( agent.Surface == surfaceId )
        {
            agent.CrowdIndex = -1;
            agent.Surface = 0;
        }
    }
}


void NavigationCore::EnsureCrowd( SurfaceRecord& InRecord, float InRadius )
{
    if( !InRecord.Mesh || !InRecord.Mesh->IsValid() )
    {
        return;
    }
    if( InRecord.Crowd && InRadius <= InRecord.CrowdRadius )
    {
        return;
    }
    const float radius = std::max( { InRadius, InRecord.CrowdRadius, InRecord.Mesh->GetData().AgentRadius, 0.1f } );
    DestroyCrowd( InRecord );
    InRecord.Crowd = dtAllocCrowd();
    if( !InRecord.Crowd->init( kMaxCrowdAgents, radius, InRecord.Mesh->GetDetourMesh() ) )
    {
        dtFreeCrowd( InRecord.Crowd );
        InRecord.Crowd = nullptr;
        return;
    }
    InRecord.CrowdRadius = radius;

    // Avoidance quality presets (Low, Medium, High), as in the Recast demo.
    dtObstacleAvoidanceParams params = *InRecord.Crowd->getObstacleAvoidanceParams( 0 );
    params.velBias = 0.5f;
    params.adaptiveDivs = 5;
    params.adaptiveRings = 2;
    params.adaptiveDepth = 1;
    InRecord.Crowd->setObstacleAvoidanceParams( 0, &params );
    params.adaptiveDepth = 2;
    InRecord.Crowd->setObstacleAvoidanceParams( 1, &params );
    params.adaptiveDivs = 7;
    params.adaptiveRings = 3;
    params.adaptiveDepth = 3;
    InRecord.Crowd->setObstacleAvoidanceParams( 2, &params );
}


int NavigationCore::FilterSlot( SurfaceRecord& InRecord, uint32_t InAreaMask )
{
    for( size_t i = 0; i < InRecord.FilterMasks.size(); ++i )
    {
        if( InRecord.FilterMasks[i] == InAreaMask )
        {
            return static_cast<int>( i );
        }
    }
    if( !InRecord.Crowd || InRecord.FilterMasks.size() >= static_cast<size_t>( DT_CROWD_MAX_QUERY_FILTER_TYPE ) )
    {
        return 0;
    }
    const int slot = static_cast<int>( InRecord.FilterMasks.size() );
    InRecord.FilterMasks.push_back( InAreaMask );
    NavQueryFilter filter;
    filter.AreaMask = InAreaMask;
    NavMesh::MakeFilter( filter, *InRecord.Crowd->getEditableFilter( slot ) );
    return slot;
}


// ------------------------------------------------------------------------------------------ Baking

NavBuildInput NavigationCore::GatherInput( NavMeshSurface& InSurface )
{
    OPTICK_EVENT( "NavigationCore::GatherInput" );
    NavBuildInput input;
    if( !InSurface.Parent )
    {
        return input;
    }
    Entity& surfaceEntity = *InSurface.Parent.Get();
    World& world = GetWorld();
    const glm::mat4 surfaceMatrix = surfaceEntity.GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix();

    if( InSurface.Collect == NavCollectObjects::Volume )
    {
        std::vector<Vector3> corners;
        std::vector<uint32_t> unused;
        BoxGeometry( InSurface.VolumeCenter, InSurface.VolumeSize, corners, unused );
        for( const Vector3& corner : corners )
        {
            input.Bounds.Encapsulate( TransformPoint( surfaceMatrix, corner ) );
        }
    }

    auto isUnder = [&surfaceEntity]( Entity& InEntity ) {
        for( Transform* transform = &InEntity.GetComponent<Transform>(); transform; transform = transform->GetParentTransform() )
        {
            if( transform->Parent && transform->Parent.Get() == &surfaceEntity )
            {
                return true;
            }
        }
        return false;
    };
    // The modifier affecting an entity: its own, or the nearest ancestor's that applies to children.
    auto modifierFor = []( Entity& InEntity ) -> NavMeshModifier* {
        if( NavMeshModifier* own = Enabled<NavMeshModifier>( InEntity ) )
        {
            return own;
        }
        for( Transform* parent = InEntity.GetComponent<Transform>().GetParentTransform(); parent; parent = parent->GetParentTransform() )
        {
            if( parent->Parent )
            {
                NavMeshModifier* modifier = Enabled<NavMeshModifier>( *parent->Parent.Get() );
                if( modifier && modifier->ApplyToChildren )
                {
                    return modifier;
                }
            }
        }
        return nullptr;
    };
    // Moving things aren't part of the static navmesh.
    auto isDynamic = []( Entity& InEntity ) {
        if( Enabled<NavMeshAgent>( InEntity ) || Enabled<CharacterController>( InEntity ) || Enabled<NavMeshObstacle>( InEntity ) )
        {
            return true;
        }
        for( Transform* transform = &InEntity.GetComponent<Transform>(); transform; transform = transform->GetParentTransform() )
        {
            if( transform->Parent )
            {
                if( Rigidbody* body = Enabled<Rigidbody>( *transform->Parent.Get() ) )
                {
                    return body->Type != BodyType::Static;
                }
            }
        }
        return false;
    };
    // Returns the area to bake an entity's geometry with, or false to skip it.
    auto classify = [&]( Entity& InEntity, uint8_t& OutArea ) {
        if( !InEntity.IsActiveInHierarchy() || ( InSurface.IncludeLayers & ( 1u << ( InEntity.GetLayer() & 31u ) ) ) == 0 )
        {
            return false;
        }
        if( InSurface.Collect == NavCollectObjects::Children && !isUnder( InEntity ) )
        {
            return false;
        }
        if( isDynamic( InEntity ) )
        {
            return false;
        }
        OutArea = static_cast<uint8_t>( std::clamp( InSurface.DefaultArea, 0, NavAreas::kCount - 1 ) );
        if( NavMeshModifier* modifier = modifierFor( InEntity ) )
        {
            if( modifier->IgnoreFromBuild )
            {
                return false;
            }
            if( modifier->OverrideArea )
            {
                OutArea = static_cast<uint8_t>( std::clamp( modifier->Area, 0, NavAreas::kCount - 1 ) );
            }
        }
        return true;
    };

    TriangleSink sink{ input };
    std::vector<Vector3> points;
    std::vector<uint32_t> indices;
    const bool useColliders = InSurface.UseGeometry != NavCollectGeometry::RenderMeshes;
    const bool useMeshes = InSurface.UseGeometry != NavCollectGeometry::PhysicsColliders;
    if( useColliders )
    {
        world.Each<BoxCollider, Transform>( [&]( Entity& entity, BoxCollider& box, Transform& transform ) {
            uint8_t area = 0;
            if( box.IsTrigger || !classify( entity, area ) )
            {
                return;
            }
            points.clear();
            indices.clear();
            BoxGeometry( box.Center, box.Size, points, indices );
            sink.Add( transform.GetLocalToWorldMatrix().GetInternalMatrix(), points, indices, area );
        } );
        world.Each<SphereCollider, Transform>( [&]( Entity& entity, SphereCollider& sphere, Transform& transform ) {
            uint8_t area = 0;
            if( sphere.IsTrigger || !classify( entity, area ) )
            {
                return;
            }
            points.clear();
            indices.clear();
            CapsuleGeometry( sphere.Center, sphere.Radius, 0.f, glm::mat3( 1.f ), points, indices );
            sink.Add( transform.GetLocalToWorldMatrix().GetInternalMatrix(), points, indices, area );
        } );
        world.Each<CapsuleCollider, Transform>( [&]( Entity& entity, CapsuleCollider& capsule, Transform& transform ) {
            uint8_t area = 0;
            if( capsule.IsTrigger || !classify( entity, area ) )
            {
                return;
            }
            glm::mat3 frame( 1.f );
            if( capsule.Direction == CapsuleAxis::X )
            {
                frame = glm::mat3( glm::vec3( 0.f, -1.f, 0.f ), glm::vec3( 1.f, 0.f, 0.f ), glm::vec3( 0.f, 0.f, 1.f ) );
            }
            else if( capsule.Direction == CapsuleAxis::Z )
            {
                frame = glm::mat3( glm::vec3( 1.f, 0.f, 0.f ), glm::vec3( 0.f, 0.f, 1.f ), glm::vec3( 0.f, -1.f, 0.f ) );
            }
            points.clear();
            indices.clear();
            CapsuleGeometry( capsule.Center, capsule.Radius, std::max( capsule.Height * 0.5f - capsule.Radius, 0.f ), frame, points, indices );
            sink.Add( transform.GetLocalToWorldMatrix().GetInternalMatrix(), points, indices, area );
        } );
        world.Each<MeshCollider, Transform>( [&]( Entity& entity, MeshCollider& collider, Transform& transform ) {
            uint8_t area = 0;
            Mesh* mesh = entity.TryGetComponent<Mesh>();
            if( collider.IsTrigger || !mesh || !mesh->MeshReferece || !classify( entity, area ) )
            {
                return;
            }
            sink.Add( transform.GetLocalToWorldMatrix().GetInternalMatrix(), mesh->MeshReferece->CollisionPositions, mesh->MeshReferece->CollisionIndices, area );
        } );
    }
    if( useMeshes )
    {
        world.Each<Mesh, Transform>( [&]( Entity& entity, Mesh& mesh, Transform& transform ) {
            uint8_t area = 0;
            if( !mesh.MeshReferece || mesh.IsSkinned() || !classify( entity, area ) )
            {
                return;
            }
            // With both sources, a mesh that is also a mesh collider is already in.
            if( useColliders && Enabled<MeshCollider>( entity ) )
            {
                return;
            }
            sink.Add( transform.GetLocalToWorldMatrix().GetInternalMatrix(), mesh.MeshReferece->CollisionPositions, mesh.MeshReferece->CollisionIndices, area );
        } );
    }

    world.Each<NavMeshModifierVolume, Transform>( [&]( Entity& entity, NavMeshModifierVolume& volume, Transform& transform ) {
        if( InSurface.Collect == NavCollectObjects::Children && !isUnder( entity ) )
        {
            return;
        }
        std::vector<Vector3> corners;
        std::vector<uint32_t> unused;
        BoxGeometry( volume.Center, volume.Size, corners, unused );
        const glm::mat4 matrix = transform.GetLocalToWorldMatrix().GetInternalMatrix();
        NavBuildInput::Volume out;
        out.MinY = FLT_MAX;
        out.MaxY = -FLT_MAX;
        for( Vector3& corner : corners )
        {
            corner = TransformPoint( matrix, corner );
            out.MinY = std::min( out.MinY, corner.y );
            out.MaxY = std::max( out.MaxY, corner.y );
        }
        out.Outline = HullXZ( corners );
        out.Area = static_cast<uint8_t>( std::clamp( volume.Area, 0, NavAreas::kCount - 1 ) );
        input.Volumes.push_back( std::move( out ) );
    } );

    uint32_t linkId = 0;
    world.Each<NavMeshLink, Transform>( [&]( Entity& entity, NavMeshLink& link, Transform& transform ) {
        if( InSurface.Collect == NavCollectObjects::Children && !isUnder( entity ) )
        {
            return;
        }
        const glm::mat4 matrix = transform.GetLocalToWorldMatrix().GetInternalMatrix();
        NavBuildInput::Link out;
        out.Start = TransformPoint( matrix, link.StartPoint );
        out.End = TransformPoint( matrix, link.EndPoint );
        out.Radius = link.Radius;
        out.Bidirectional = link.Bidirectional;
        out.Area = static_cast<uint8_t>( std::clamp( link.Area, 0, NavAreas::kCount - 1 ) );
        out.UserId = linkId++;
        input.Links.push_back( out );
    } );
    return input;
}


bool NavigationCore::Bake( NavMeshSurface& InSurface, bool InAsync )
{
    if( !InSurface.Parent )
    {
        return false;
    }
    Entity& entity = *InSurface.Parent.Get();
    SurfaceRecord& record = m_surfaces[entity.GetId().Value()];
    record.Surface = entity.GetHandle();
    CancelBake( record );

    NavBuildInput input = GatherInput( InSurface );
    const NavBuildSettings settings = InSurface.GetBuildSettings();
    auto job = std::make_unique<BakeJob>();
#if USING( ME_TOOLS )
    job->SavePath = GetDataPath( InSurface );
#endif
    BakeJob* raw = job.get();
    auto run = [raw, input = std::move( input ), settings]() {
        NavBuildOptions options;
        options.Cancel = &raw->Cancel;
        options.TilesDone = &raw->TilesDone;
        options.TilesTotal = &raw->TilesTotal;
        raw->Result = BuildNavMesh( input, settings, options );
        raw->Done = true;
    };
    if( InAsync )
    {
        job->Thread = std::thread( std::move( run ) );
    }
    else
    {
        run();
    }
    record.Bake = std::move( job );
    record.LastError.clear();
    if( !InAsync )
    {
        PollBakes();
    }
    return true;
}


void NavigationCore::BakeAll( bool InAsync )
{
    for( Entity& entity : GetEntities() )
    {
        if( NavMeshSurface* surface = Enabled<NavMeshSurface>( entity ) )
        {
            Bake( *surface, InAsync );
        }
    }
}


void NavigationCore::CancelBake( SurfaceRecord& InRecord )
{
    if( !InRecord.Bake )
    {
        return;
    }
    InRecord.Bake->Cancel = true;
    if( InRecord.Bake->Thread.joinable() )
    {
        InRecord.Bake->Thread.join();
    }
    InRecord.Bake.reset();
}


void NavigationCore::PollBakes()
{
    for( auto& [id, record] : m_surfaces )
    {
        if( !record.Bake || !record.Bake->Done )
        {
            continue;
        }
        std::unique_ptr<BakeJob> job = std::move( record.Bake );
        if( job->Thread.joinable() )
        {
            job->Thread.join();
        }
        const std::string name = record.Surface ? record.Surface->GetName() : std::string( "?" );
        NavBuildResult& result = job->Result;
        if( !result.Success )
        {
            record.LastError = result.Error;
            if( result.Error != "cancelled" )
            {
                YIKES( "NavMesh bake failed for '" + name + "': " + result.Error );
            }
            continue;
        }
        SetMesh( record, result.Data );
        record.BakeMilliseconds = result.Milliseconds;
        record.LastError.clear();
        record.OutOfDateCheckedAt = -1.0;
        // Obstacles carve the new navmesh from scratch.
        record.CarveInput.reset();
        record.Carved = false;
        record.PendingTiles.clear();
        for( auto& [obstacleId, obstacle] : m_obstacles )
        {
            obstacle.HasCarved = false;
            obstacle.Dirty = true;
        }
        if( !job->SavePath.empty() )
        {
            const Path file( job->SavePath );
            std::error_code error;
            std::filesystem::create_directories( std::filesystem::path( file.FullPath ).parent_path(), error );
            if( !result.Data.Save( file.FullPath ) )
            {
                record.LastError = "couldn't write " + job->SavePath;
                YIKES( "NavMesh: " + record.LastError );
            }
            record.LoadedPath = job->SavePath;
            record.LoadAttempted = true;
        }
        char line[256];
        std::snprintf( line, sizeof( line ), "NavMesh baked: '%s' (%d tiles, %d polygons) in %.1f ms", name.c_str(), result.TileCount, result.PolyCount, result.Milliseconds );
        CLog::Log( CLog::LogType::Info, line );
    }
}


void NavigationCore::WaitForBakes()
{
    for( auto& [id, record] : m_surfaces )
    {
        if( record.Bake && record.Bake->Thread.joinable() )
        {
            record.Bake->Thread.join();
        }
        if( record.Carve && record.Carve->Thread.joinable() )
        {
            record.Carve->Thread.join();
        }
    }
    PollBakes();
    PollCarves();
}


// --------------------------------------------------------------------------------------- Obstacles

NavBuildInput::Volume NavigationCore::ObstacleFootprint( NavMeshObstacle& InObstacle, Transform& InTransform )
{
    const glm::mat4 matrix = InTransform.GetLocalToWorldMatrix().GetInternalMatrix();
    std::vector<Vector3> corners;
    const Vector3 half = InObstacle.Size * 0.5f;
    if( InObstacle.Shape == NavObstacleShape::Cylinder )
    {
        constexpr int kSides = 12;
        for( int i = 0; i < kSides; ++i )
        {
            const float angle = 2.f * kPi * i / kSides;
            for( float y : { -half.y, half.y } )
            {
                corners.push_back( InObstacle.Center + Vector3( std::cos( angle ) * half.x, y, std::sin( angle ) * half.x ) );
            }
        }
    }
    else
    {
        std::vector<uint32_t> unused;
        BoxGeometry( InObstacle.Center, InObstacle.Size, corners, unused );
    }
    NavBuildInput::Volume volume;
    volume.MinY = FLT_MAX;
    volume.MaxY = -FLT_MAX;
    for( Vector3& corner : corners )
    {
        corner = TransformPoint( matrix, corner );
        volume.MinY = std::min( volume.MinY, corner.y );
        volume.MaxY = std::max( volume.MaxY, corner.y );
    }
    volume.Outline = HullXZ( corners );
    volume.Area = NavAreas::NotWalkable;
    return volume;
}


void NavigationCore::SyncObstacles( float InDeltaSeconds )
{
    for( auto& [id, record] : m_obstacles )
    {
        record.Seen = false;
    }
    GetWorld().Each<NavMeshObstacle, Transform>( [&]( Entity& entity, NavMeshObstacle& obstacle, Transform& transform ) {
        ObstacleRecord& record = m_obstacles[entity.GetId().Value()];
        record.Obstacle = entity.GetHandle();
        record.Seen = true;
        NavBuildInput::Volume now = ObstacleFootprint( obstacle, transform );
        bool moved = now.Outline.size() != record.Current.Outline.size() || std::fabs( now.MinY - record.Current.MinY ) > obstacle.MoveThreshold;
        for( size_t i = 0; !moved && i < now.Outline.size(); ++i )
        {
            moved = ( now.Outline[i] - record.Current.Outline[i] ).Length() > obstacle.MoveThreshold;
        }
        if( moved )
        {
            record.Current = std::move( now );
            record.StillTime = 0.f;
            record.Dirty = true;
        }
        else
        {
            record.StillTime += InDeltaSeconds;
        }
        if( record.Dirty && record.StillTime >= obstacle.CarveDelay )
        {
            // Rebuild where it was and where it is.
            if( record.HasCarved )
            {
                MarkTiles( record.Carved );
            }
            MarkTiles( record.Current );
            record.Carved = record.Current;
            record.HasCarved = true;
            record.Dirty = false;
        }
    } );
    for( auto it = m_obstacles.begin(); it != m_obstacles.end(); )
    {
        if( !it->second.Seen )
        {
            if( it->second.HasCarved )
            {
                MarkTiles( it->second.Carved );
            }
            it = m_obstacles.erase( it );
        }
        else
        {
            ++it;
        }
    }
}


void NavigationCore::MarkTiles( const NavBuildInput::Volume& InVolume )
{
    if( InVolume.Outline.empty() )
    {
        return;
    }
    float minX = FLT_MAX;
    float minZ = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxZ = -FLT_MAX;
    for( const Vector3& point : InVolume.Outline )
    {
        minX = std::min( minX, point.x );
        maxX = std::max( maxX, point.x );
        minZ = std::min( minZ, point.z );
        maxZ = std::max( maxZ, point.z );
    }
    for( auto& [id, record] : m_surfaces )
    {
        if( !record.Mesh || !record.Mesh->IsValid() )
        {
            continue;
        }
        const NavMeshData& data = record.Carved ? record.BaseData : record.Mesh->GetData();
        // The obstacle grows by the agent radius, and tiles read a border around themselves.
        const float margin = data.AgentRadius * 2.f + 0.5f;
        const int x0 = std::max( 0, static_cast<int>( std::floor( ( minX - margin - data.Origin[0] ) / data.TileWidth ) ) );
        const int x1 = static_cast<int>( std::floor( ( maxX + margin - data.Origin[0] ) / data.TileWidth ) );
        const int z0 = std::max( 0, static_cast<int>( std::floor( ( minZ - margin - data.Origin[2] ) / data.TileHeight ) ) );
        const int z1 = static_cast<int>( std::floor( ( maxZ + margin - data.Origin[2] ) / data.TileHeight ) );
        for( int z = z0; z <= z1; ++z )
        {
            for( int x = x0; x <= x1; ++x )
            {
                record.PendingTiles.emplace( x, z );
            }
        }
    }
}


void NavigationCore::StartCarves()
{
    for( auto& [id, record] : m_surfaces )
    {
        if( record.Carve || record.Bake || record.PendingTiles.empty() || !record.Mesh || !record.Mesh->IsValid() || !record.Surface )
        {
            continue;
        }
        NavMeshSurface* surface = record.Surface->TryGetComponent<NavMeshSurface>();
        if( !surface )
        {
            continue;
        }
        if( !record.CarveInput )
        {
            record.CarveInput = std::make_unique<NavBuildInput>( GatherInput( *surface ) );
            record.CarveSettings = surface->GetBuildSettings();
        }
        if( !record.Carved )
        {
            record.BaseData = record.Mesh->GetData();
            record.Carved = true;
        }
        NavBuildInput input = *record.CarveInput;
        for( const auto& [obstacleId, obstacle] : m_obstacles )
        {
            if( obstacle.HasCarved )
            {
                NavBuildInput::Volume volume = obstacle.Carved;
                volume.Inflate = record.BaseData.AgentRadius;
                input.Volumes.push_back( std::move( volume ) );
            }
        }
        NavMeshData grid = record.BaseData;
        grid.Tiles.clear();
        std::vector<std::pair<int, int>> tiles( record.PendingTiles.begin(), record.PendingTiles.end() );
        record.PendingTiles.clear();

        auto job = std::make_unique<BakeJob>();
        BakeJob* raw = job.get();
        job->Thread = std::thread( [raw, input = std::move( input ), settings = record.CarveSettings, grid = std::move( grid ), tiles = std::move( tiles )]() {
            NavBuildOptions options;
            options.Cancel = &raw->Cancel;
            options.Grid = &grid;
            options.OnlyTiles = tiles;
            options.ThreadCount = 2;    // a handful of tiles; leave the cores to the game
            raw->Result = BuildNavMesh( input, settings, options );
            raw->Done = true;
        } );
        record.Carve = std::move( job );
    }
}


void NavigationCore::PollCarves()
{
    for( auto& [id, record] : m_surfaces )
    {
        if( !record.Carve || !record.Carve->Done )
        {
            continue;
        }
        std::unique_ptr<BakeJob> job = std::move( record.Carve );
        if( job->Thread.joinable() )
        {
            job->Thread.join();
        }
        if( !job->Result.Success )
        {
            if( job->Result.Error != "cancelled" )
            {
                YIKES( "NavMesh obstacle carve failed: " + job->Result.Error );
            }
            continue;
        }
        if( record.Mesh && record.Carved )
        {
            record.Mesh->ReplaceTiles( job->Result.RebuiltTiles, job->Result.Data.Tiles );
        }
    }
}


void NavigationCore::RestoreBakedMeshes()
{
    for( auto& [id, record] : m_surfaces )
    {
        if( record.Carve )
        {
            record.Carve->Cancel = true;
            if( record.Carve->Thread.joinable() )
            {
                record.Carve->Thread.join();
            }
            record.Carve.reset();
        }
        record.PendingTiles.clear();
        if( record.Carved )
        {
            SetMesh( record, record.BaseData );
            record.Carved = false;
        }
        record.CarveInput.reset();
    }
}


bool NavigationCore::IsCarving() const
{
    for( const auto& [id, record] : m_surfaces )
    {
        if( record.Carve || !record.PendingTiles.empty() )
        {
            return true;
        }
    }
    return false;
}


bool NavigationCore::IsOutOfDate( const NavMeshSurface& InSurface, bool InRecheckNow )
{
    SurfaceRecord* record = FindSurface( InSurface );
    if( !record || !record->Mesh || !record->Mesh->IsValid() || record->Bake || !InSurface.Parent )
    {
        return false;
    }
    const double now = std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    if( InRecheckNow || record->OutOfDateCheckedAt < 0.0 || now - record->OutOfDateCheckedAt > 3.0 )
    {
        NavMeshSurface& surface = *InSurface.Parent->TryGetComponent<NavMeshSurface>();
        const uint64_t hash = GatherInput( surface ).Hash() ^ ( surface.GetBuildSettings().Hash() * 31ull );
        const NavMeshData& data = record->Carved ? record->BaseData : record->Mesh->GetData();
        record->OutOfDate = hash != data.SourceHash;
        record->OutOfDateCheckedAt = now;
    }
    return record->OutOfDate;
}


void NavigationCore::ClearNavMesh( NavMeshSurface& InSurface, bool InDeleteFile )
{
    SurfaceRecord* record = FindSurface( InSurface );
    if( record )
    {
        CancelBake( *record );
        DestroyCrowd( *record );
        record->Mesh.reset();
        record->LastError.clear();
    }
    const std::string path = GetDataPath( InSurface );
    if( InDeleteFile && !path.empty() )
    {
        const Path file( path );
        std::error_code error;
        std::filesystem::remove( file.FullPath, error );
    }
}


bool NavigationCore::IsBaking() const
{
    for( const auto& [id, record] : m_surfaces )
    {
        if( record.Bake )
        {
            return true;
        }
    }
    return false;
}


bool NavigationCore::IsBaking( const NavMeshSurface& InSurface ) const
{
    const SurfaceRecord* record = FindSurface( InSurface );
    return record && record->Bake;
}


float NavigationCore::GetBakeProgress( const NavMeshSurface& InSurface ) const
{
    const SurfaceRecord* record = FindSurface( InSurface );
    if( !record || !record->Bake )
    {
        return 0.f;
    }
    const int total = record->Bake->TilesTotal.load();
    return total > 0 ? static_cast<float>( record->Bake->TilesDone.load() ) / total : 0.f;
}


NavigationCore::SurfaceInfo NavigationCore::GetSurfaceInfo( const NavMeshSurface& InSurface ) const
{
    SurfaceInfo info;
    info.DataPath = GetDataPath( InSurface );
    const SurfaceRecord* record = FindSurface( InSurface );
    if( !record )
    {
        return info;
    }
    info.Loaded = record->Mesh && record->Mesh->IsValid();
    info.Baking = record->Bake != nullptr;
    info.Progress = GetBakeProgress( InSurface );
    info.Tiles = info.Loaded ? record->Mesh->GetTileCount() : 0;
    info.Polygons = info.Loaded ? record->Mesh->GetPolyCount() : 0;
    info.BakeMilliseconds = record->BakeMilliseconds;
    info.LastError = record->LastError;
    const uint64_t surfaceId = record->Surface ? record->Surface->GetId().Value() : 0;
    for( const auto& [id, agent] : m_agents )
    {
        info.Agents += agent.Surface == surfaceId && agent.CrowdIndex >= 0 ? 1 : 0;
    }
    return info;
}


void NavigationCore::MarkInspected( const NavMeshSurface& InSurface )
{
    if( SurfaceRecord* record = FindSurface( InSurface ) )
    {
        record->Inspected = true;
    }
}


// ------------------------------------------------------------------------------------------ Agents

NavigationCore::SurfaceRecord* NavigationCore::SurfaceFor( const Vector3& InPosition, float InAgentRadius )
{
    SurfaceRecord* best = nullptr;
    float bestScore = FLT_MAX;
    for( auto& [id, record] : m_surfaces )
    {
        if( !record.Mesh || !record.Mesh->IsValid() )
        {
            continue;
        }
        // Prefer the navmesh under the agent, then the one baked for the closest agent size.
        const AABB bounds = record.Mesh->GetBounds();
        const bool inside = InPosition.x >= bounds.Min.x - 1.f && InPosition.x <= bounds.Max.x + 1.f && InPosition.z >= bounds.Min.z - 1.f && InPosition.z <= bounds.Max.z + 1.f
            && InPosition.y >= bounds.Min.y - 2.f && InPosition.y <= bounds.Max.y + 2.f;
        const float sizeMismatch = std::fabs( record.Mesh->GetData().AgentRadius - InAgentRadius );
        const float score = ( inside ? 0.f : 1000.f ) + sizeMismatch;
        if( score < bestScore )
        {
            bestScore = score;
            best = &record;
        }
    }
    return best;
}


void NavigationCore::RemoveAgent( AgentRecord& InAgent )
{
    if( InAgent.CrowdIndex >= 0 )
    {
        auto surface = m_surfaces.find( InAgent.Surface );
        if( surface != m_surfaces.end() && surface->second.Crowd )
        {
            surface->second.Crowd->removeAgent( InAgent.CrowdIndex );
        }
    }
    InAgent.CrowdIndex = -1;
    InAgent.Surface = 0;
}


bool NavigationCore::AddAgent( AgentRecord& InAgent, NavMeshAgent& InComponent, Entity& InEntity )
{
    Transform& transform = InEntity.GetComponent<Transform>();
    const Vector3 position = transform.GetWorldPosition() - Vector3( 0.f, InComponent.BaseOffset, 0.f );
    SurfaceRecord* surface = nullptr;
    if( InAgent.CrowdIndex >= 0 )
    {
        auto it = m_surfaces.find( InAgent.Surface );
        surface = it != m_surfaces.end() ? &it->second : nullptr;
    }
    if( !surface )
    {
        InAgent.CrowdIndex = -1;
        surface = SurfaceFor( position, InComponent.Radius );
    }
    if( !surface )
    {
        return false;
    }
    EnsureCrowd( *surface, InComponent.Radius );
    if( !surface->Crowd )
    {
        return false;
    }
    dtCrowdAgentParams params{};
    params.radius = InComponent.Radius;
    params.height = InComponent.Height;
    params.maxAcceleration = InComponent.Acceleration;
    params.maxSpeed = InComponent.Speed;
    params.collisionQueryRange = InComponent.Radius * 12.f;
    params.pathOptimizationRange = InComponent.Radius * 30.f;
    params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_SEPARATION;
    if( InComponent.Avoidance != NavAvoidanceQuality::None )
    {
        params.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
        params.obstacleAvoidanceType = static_cast<unsigned char>( static_cast<int>( InComponent.Avoidance ) - 1 );
    }
    params.separationWeight = 2.f;
    params.queryFilterType = static_cast<unsigned char>( FilterSlot( *surface, InComponent.AreaMask ) );

    const uint64_t hash = std::hash<float>()( params.radius ) ^ ( std::hash<float>()( params.height ) << 1 ) ^ ( std::hash<float>()( params.maxAcceleration ) << 2 )
        ^ ( std::hash<float>()( params.maxSpeed ) << 3 ) ^ ( static_cast<uint64_t>( params.updateFlags ) << 40 ) ^ ( static_cast<uint64_t>( params.obstacleAvoidanceType ) << 48 )
        ^ ( static_cast<uint64_t>( params.queryFilterType ) << 56 );
    if( InAgent.CrowdIndex >= 0 )
    {
        if( hash != InAgent.ParamsHash )
        {
            surface->Crowd->updateAgentParameters( InAgent.CrowdIndex, &params );
            InAgent.ParamsHash = hash;
        }
        return true;
    }
    const int index = surface->Crowd->addAgent( &position.x, &params );
    if( index < 0 )
    {
        return false;
    }
    InAgent.CrowdIndex = index;
    InAgent.Surface = surface->Surface->GetId().Value();
    InAgent.ParamsHash = hash;
    InComponent.m_position = position;
    InComponent.m_onNavMesh = surface->Crowd->getAgent( index )->state != DT_CROWDAGENT_STATE_INVALID;
    // A destination set before the agent joined (e.g. in OnStart) still applies.
    if( InComponent.m_hasPath && !InComponent.m_requestPending )
    {
        InComponent.m_requestPending = true;
    }
    return true;
}


void NavigationCore::SyncAgents()
{
    for( Entity& entity : GetEntities() )
    {
        NavMeshAgent* agent = Enabled<NavMeshAgent>( entity );
        const uint64_t id = entity.GetId().Value();
        if( !agent || !entity.IsActiveInHierarchy() )
        {
            auto it = m_agents.find( id );
            if( it != m_agents.end() )
            {
                RemoveAgent( it->second );
                m_agents.erase( it );
            }
            continue;
        }
        AgentRecord& record = m_agents[id];
        record.Agent = entity.GetHandle();
        if( record.CrowdIndex >= 0 && m_surfaces.find( record.Surface ) == m_surfaces.end() )
        {
            record.CrowdIndex = -1;
        }
        AddAgent( record, *agent, entity );
    }
}


void NavigationCore::StepAgents( float InDeltaSeconds )
{
    OPTICK_EVENT( "NavigationCore::StepAgents" );
    // Requests first, then one crowd update per navmesh, then read the results back.
    for( auto& [id, record] : m_agents )
    {
        auto surfaceIt = m_surfaces.find( record.Surface );
        if( record.CrowdIndex < 0 || !record.Agent || surfaceIt == m_surfaces.end() || !surfaceIt->second.Crowd )
        {
            continue;
        }
        NavMeshAgent* agent = record.Agent->TryGetComponent<NavMeshAgent>();
        if( !agent )
        {
            continue;
        }
        dtCrowd& crowd = *surfaceIt->second.Crowd;
        if( agent->m_warpRequested )
        {
            agent->m_warpRequested = false;
            crowd.removeAgent( record.CrowdIndex );
            record.CrowdIndex = -1;
            Transform& transform = record.Agent->GetComponent<Transform>();
            transform.SetWorldPosition( agent->m_warpPosition + Vector3( 0.f, agent->BaseOffset, 0.f ) );
            AddAgent( record, *agent, *record.Agent.Get() );
            continue;
        }
        if( agent->m_resetRequested )
        {
            agent->m_resetRequested = false;
            crowd.resetMoveTarget( record.CrowdIndex );
        }
        if( agent->m_requestPending )
        {
            agent->m_requestPending = false;
            const dtQueryFilter* filter = crowd.getFilter( crowd.getAgent( record.CrowdIndex )->params.queryFilterType );
            dtPolyRef ref = 0;
            float target[3];
            crowd.getNavMeshQuery()->findNearestPoly( &agent->m_destination.x, crowd.getQueryHalfExtents(), filter, &ref, target );
            if( ref && crowd.requestMoveTarget( record.CrowdIndex, ref, target ) )
            {
                agent->m_hasPath = true;
                agent->m_pathStatus = NavPathStatus::Complete;
            }
            else
            {
                agent->m_hasPath = false;
                agent->m_pathStatus = NavPathStatus::Invalid;
            }
        }
        const dtCrowdAgent* crowdAgent = crowd.getAgent( record.CrowdIndex );
        if( agent->m_stopped && crowdAgent->targetState != DT_CROWDAGENT_TARGET_VELOCITY )
        {
            const float zero[3] = { 0.f, 0.f, 0.f };
            crowd.requestMoveVelocity( record.CrowdIndex, zero );
        }
        else if( !agent->m_stopped && crowdAgent->targetState == DT_CROWDAGENT_TARGET_VELOCITY && agent->m_hasPath && !agent->m_arrived )
        {
            agent->m_requestPending = true;   // resume the path on the next update
        }
    }

    for( auto& [id, surface] : m_surfaces )
    {
        if( surface.Crowd )
        {
            surface.Crowd->update( InDeltaSeconds, nullptr );
        }
    }

    for( auto& [id, record] : m_agents )
    {
        auto surfaceIt = m_surfaces.find( record.Surface );
        if( record.CrowdIndex < 0 || !record.Agent || surfaceIt == m_surfaces.end() || !surfaceIt->second.Crowd )
        {
            continue;
        }
        SurfaceRecord& surface = surfaceIt->second;
        NavMeshAgent* agent = record.Agent->TryGetComponent<NavMeshAgent>();
        if( !agent )
        {
            continue;
        }
        const dtCrowdAgent* crowdAgent = surface.Crowd->getAgent( record.CrowdIndex );
        if( !crowdAgent || !crowdAgent->active )
        {
            continue;
        }
        const Vector3 position( crowdAgent->npos[0], crowdAgent->npos[1], crowdAgent->npos[2] );
        agent->m_position = position;
        agent->m_velocity = Vector3( crowdAgent->vel[0], crowdAgent->vel[1], crowdAgent->vel[2] );
        agent->m_desiredVelocity = Vector3( crowdAgent->dvel[0], crowdAgent->dvel[1], crowdAgent->dvel[2] );
        agent->m_onNavMesh = crowdAgent->state != DT_CROWDAGENT_STATE_INVALID;
        const bool onLink = crowdAgent->state == DT_CROWDAGENT_STATE_OFFMESH;
        if( onLink && !agent->m_onLink )
        {
            agent->m_linkStart = position;
        }
        agent->m_onLink = onLink;

        const unsigned char targetState = crowdAgent->targetState;
        const bool waiting = targetState == DT_CROWDAGENT_TARGET_REQUESTING || targetState == DT_CROWDAGENT_TARGET_WAITING_FOR_QUEUE || targetState == DT_CROWDAGENT_TARGET_WAITING_FOR_PATH;
        if( targetState == DT_CROWDAGENT_TARGET_FAILED )
        {
            agent->m_hasPath = false;
            agent->m_pathStatus = NavPathStatus::Invalid;
        }
        else if( targetState == DT_CROWDAGENT_TARGET_VALID )
        {
            agent->m_pathStatus = crowdAgent->partial ? NavPathStatus::Partial : NavPathStatus::Complete;
        }
        agent->m_pathPending = waiting;

        // Remaining distance along the corners the crowd keeps (exact when the path ends within them).
        if( agent->m_hasPath && targetState == DT_CROWDAGENT_TARGET_VALID )
        {
            float remaining = 0.f;
            Vector3 previous = position;
            for( int i = 0; i < crowdAgent->ncorners; ++i )
            {
                const Vector3 corner( crowdAgent->cornerVerts[i * 3], crowdAgent->cornerVerts[i * 3 + 1], crowdAgent->cornerVerts[i * 3 + 2] );
                remaining += ( corner - previous ).Length();
                previous = corner;
            }
            const bool endsHere = crowdAgent->ncorners > 0 && ( crowdAgent->cornerFlags[crowdAgent->ncorners - 1] & DT_STRAIGHTPATH_END );
            if( !endsHere )
            {
                remaining += ( Vector3( crowdAgent->targetPos[0], crowdAgent->targetPos[1], crowdAgent->targetPos[2] ) - previous ).Length();
            }
            agent->m_remainingDistance = remaining;
            const float arriveDistance = std::max( agent->StoppingDistance, 0.1f );
            if( !onLink && remaining <= arriveDistance )
            {
                agent->m_arrived = true;
                agent->m_hasPath = false;
                surface.Crowd->resetMoveTarget( record.CrowdIndex );
            }
        }
        else if( !agent->m_hasPath )
        {
            agent->m_remainingDistance = agent->m_arrived ? 0.f : agent->m_remainingDistance;
        }

        Transform& transform = record.Agent->GetComponent<Transform>();
        if( agent->UpdatePosition )
        {
            float lift = 0.f;
            if( onLink && agent->LinkJumpHeight > 0.f )
            {
                // The crowd slides across links; arc it. The corridor already sits at the far end.
                const float* end = crowdAgent->corridor.getPos();
                const float total = DistanceXZ( agent->m_linkStart, Vector3( end[0], end[1], end[2] ) );
                const float u = total > 0.001f ? std::clamp( DistanceXZ( agent->m_linkStart, position ) / total, 0.f, 1.f ) : 1.f;
                lift = agent->LinkJumpHeight * 4.f * u * ( 1.f - u );
            }
            transform.SetWorldPosition( position + Vector3( 0.f, agent->BaseOffset + lift, 0.f ) );
        }
        if( agent->UpdateRotation )
        {
            Vector3 direction( agent->m_velocity.x, 0.f, agent->m_velocity.z );
            if( direction.LengthSquared() > 0.01f )
            {
                direction = direction.Normalized();
                Vector3 front = transform.Front();
                front.y = 0.f;
                front = front.LengthSquared() > 0.0001f ? front.Normalized() : direction;
                const float angle = std::acos( std::clamp( front.Dot( direction ), -1.f, 1.f ) );
                const float maxStep = agent->AngularSpeed * kPi / 180.f * InDeltaSeconds;
                if( angle > maxStep && maxStep > 0.f )
                {
                    // Turn about Y towards the direction of travel.
                    const float step = ( front.Cross( direction ).y >= 0.f ? 1.f : -1.f ) * maxStep;
                    const float c = std::cos( step );
                    const float s = std::sin( step );
                    direction = Vector3( front.x * c + front.z * s, 0.f, -front.x * s + front.z * c );
                }
                transform.LookDirection( direction );
            }
        }
    }
}


// ------------------------------------------------------------------------------------------ Queries

NavMesh* NavigationCore::GetNavMesh( const Vector3& InNear ) const
{
    SurfaceRecord* record = const_cast<NavigationCore*>( this )->SurfaceFor( InNear, 0.f );
    return record ? record->Mesh.get() : nullptr;
}


NavMesh* NavigationCore::GetNavMesh( const NavMeshSurface& InSurface ) const
{
    const SurfaceRecord* record = FindSurface( InSurface );
    return record && record->Mesh && record->Mesh->IsValid() ? record->Mesh.get() : nullptr;
}


NavPathStatus NavigationCore::FindPath( const Vector3& InStart, const Vector3& InEnd, std::vector<Vector3>& OutCorners, const NavQueryFilter& InFilter ) const
{
    NavMesh* mesh = GetNavMesh( InStart );
    if( !mesh )
    {
        OutCorners.clear();
        return NavPathStatus::Invalid;
    }
    return mesh->FindPath( InStart, InEnd, OutCorners, InFilter );
}


bool NavigationCore::SamplePosition( const Vector3& InPoint, float InMaxDistance, NavMeshHit& OutHit, const NavQueryFilter& InFilter ) const
{
    NavMesh* mesh = GetNavMesh( InPoint );
    return mesh && mesh->SamplePosition( InPoint, InMaxDistance, OutHit, InFilter );
}


bool NavigationCore::Raycast( const Vector3& InStart, const Vector3& InEnd, NavMeshHit& OutHit, const NavQueryFilter& InFilter ) const
{
    NavMesh* mesh = GetNavMesh( InStart );
    return mesh && mesh->Raycast( InStart, InEnd, OutHit, InFilter );
}


bool NavigationCore::GetRandomPoint( Vector3& OutPoint, const NavQueryFilter& InFilter ) const
{
    for( const auto& [id, record] : m_surfaces )
    {
        if( record.Mesh && record.Mesh->IsValid() )
        {
            return record.Mesh->GetRandomPoint( OutPoint, InFilter );
        }
    }
    return false;
}


bool NavigationCore::GetRandomPointAround( const Vector3& InCenter, float InRadius, Vector3& OutPoint, const NavQueryFilter& InFilter ) const
{
    NavMesh* mesh = GetNavMesh( InCenter );
    return mesh && mesh->GetRandomPointAround( InCenter, InRadius, OutPoint, InFilter );
}


// -------------------------------------------------------------------------------------- Debug draw

void NavigationCore::DrawDebug()
{
    const uint8_t flags = DebugDraw::EditorOnly;
    std::vector<Vector3> triangles;
    for( auto& [id, record] : m_surfaces )
    {
        const bool draw = DebugDrawEnabled || record.Inspected;
        record.Inspected = false;
        if( !draw || !record.Mesh || !record.Mesh->IsValid() )
        {
            continue;
        }
        // Lift the overlay off the ground so it doesn't z-fight the surface it covers.
        const Vector3 lift( 0.f, 0.03f, 0.f );
        std::array<std::vector<Vector3>, NavAreas::kCount> byArea;
        record.Mesh->ForEachTriangle( [&]( const Vector3& a, const Vector3& b, const Vector3& c, uint8_t area ) {
            std::vector<Vector3>& list = byArea[area % NavAreas::kCount];
            list.push_back( a + lift );
            list.push_back( b + lift );
            list.push_back( c + lift );
        } );
        for( int area = 0; area < NavAreas::kCount; ++area )
        {
            if( !byArea[area].empty() )
            {
                DebugDraw::Triangles( byArea[area].data(), byArea[area].size(), AreaColor( static_cast<uint8_t>( area ), 0.35f ), 0.f, flags );
            }
        }
        record.Mesh->ForEachEdge( [&]( const Vector3& a, const Vector3& b, bool boundary ) {
            const Vector4 color = boundary ? Vector4( 0.75f, 0.95f, 1.f, 0.95f ) : Vector4( 0.6f, 0.85f, 1.f, 0.18f );
            DebugDraw::Line( a + lift, b + lift, color, 0.f, flags );
        } );
        record.Mesh->ForEachLink( [&]( const Vector3& a, const Vector3& b, float radius, bool bidirectional ) {
            const Vector4 color = AreaColor( NavAreas::Jump, 1.f );
            const float arc = std::max( 0.25f, DistanceXZ( a, b ) * 0.25f );
            Vector3 previous = a;
            for( int i = 1; i <= 12; ++i )
            {
                const float u = i / 12.f;
                const Vector3 point = a + ( b - a ) * u + Vector3( 0.f, arc * 4.f * u * ( 1.f - u ), 0.f );
                DebugDraw::Line( previous, point, color, 0.f, flags );
                previous = point;
            }
            DebugDraw::Circle( a, Vector3::Up, radius, color, 0.f, flags, 16 );
            DebugDraw::Circle( b, Vector3::Up, radius, bidirectional ? color : Vector4( 0.6f, 0.6f, 0.6f, 1.f ), 0.f, flags, 16 );
        } );
    }

    if( !DebugDrawEnabled )
    {
        return;
    }
    // Authoring gizmos (before a bake shows them in the mesh).
    GetWorld().Each<NavMeshLink, Transform>( [&]( Entity&, NavMeshLink& link, Transform& transform ) {
        const glm::mat4 matrix = transform.GetLocalToWorldMatrix().GetInternalMatrix();
        DebugDraw::Arrow( TransformPoint( matrix, link.StartPoint ), TransformPoint( matrix, link.EndPoint ), DebugDraw::Orange, 0.2f, 0.f, flags );
    } );
    for( const auto& [id, obstacle] : m_obstacles )
    {
        const NavBuildInput::Volume& footprint = obstacle.HasCarved ? obstacle.Carved : obstacle.Current;
        const Vector4 color = obstacle.Dirty ? DebugDraw::Yellow : DebugDraw::Red;
        for( size_t i = 0; i < footprint.Outline.size(); ++i )
        {
            const Vector3& a = footprint.Outline[i];
            const Vector3& b = footprint.Outline[( i + 1 ) % footprint.Outline.size()];
            DebugDraw::Line( Vector3( a.x, footprint.MinY + 0.03f, a.z ), Vector3( b.x, footprint.MinY + 0.03f, b.z ), color, 0.f, flags );
        }
    }
    GetWorld().Each<NavMeshModifierVolume, Transform>( [&]( Entity&, NavMeshModifierVolume& volume, Transform& transform ) {
        AABB local;
        local.Encapsulate( volume.Center - volume.Size * 0.5f );
        local.Encapsulate( volume.Center + volume.Size * 0.5f );
        DebugDraw::Box( transform.GetLocalToWorldMatrix(), local, AreaColor( static_cast<uint8_t>( volume.Area ), 1.f ), 0.f, flags );
    } );
    for( auto& [id, record] : m_agents )
    {
        if( !record.Agent )
        {
            continue;
        }
        NavMeshAgent* agent = record.Agent->TryGetComponent<NavMeshAgent>();
        if( !agent )
        {
            continue;
        }
        DebugDraw::Circle( agent->m_position, Vector3::Up, agent->Radius, agent->m_onNavMesh ? DebugDraw::Green : DebugDraw::Red, 0.f, flags, 20 );
        if( agent->m_hasPath )
        {
            DebugDraw::Line( agent->m_position, agent->m_destination, DebugDraw::Yellow, 0.f, flags );
            DebugDraw::Circle( agent->m_destination, Vector3::Up, 0.25f, DebugDraw::Yellow, 0.f, flags, 12 );
        }
    }
}


#if USING( ME_EDITOR )
void NavigationCore::OnEditorInspect()
{
    ImGui::Checkbox( "Draw Navigation", &DebugDrawEnabled );
    ImGui::Text( "%zu surface(s), %zu agent(s)", m_surfaces.size(), m_agents.size() );
    if( ImGui::Button( "Bake All" ) )
    {
        BakeAll( true );
    }
}
#endif
