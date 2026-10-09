#pragma once
#include "ECS/Core.h"
#include "ECS/CoreDetail.h"
#include "Physics/PhysicsTypes.h"
#include "Jobs/JobSystem.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class Transform;
class Rigidbody;
class CharacterController;
class PhysicsJoint;
struct ColliderSettings;
namespace Moonlight { class MeshData; }

// 3D physics (Box3D). Engine-owned: always present, builds bodies from Rigidbody / collider /
// CharacterController / PhysicsJoint components, and steps at the fixed rate while the world is
// started (play mode, game builds). Outside play mode bodies follow their Transforms, so editor
// moves and queries stay in sync.
//
// - Colliders on an entity attach to its Rigidbody, or to the nearest ancestor's (compound bodies);
//   with neither they make a static body. Triangle-mesh colliders always get their own body.
// - Dynamic bodies write their (interpolated) pose to the Transform; moving the Transform of a
//   dynamic body from code teleports it. Kinematic bodies follow their Transform smoothly.
// - Entity layers + the Project Settings collision matrix filter contacts and queries.
// - CollisionEvent fires on the main thread after each step for contacts and triggers.
class PhysicsCore final
    : public Core<PhysicsCore>
{
public:
    PhysicsCore();
    ~PhysicsCore() override;

    void Init() final;
    void OnStart() final;
    void OnStop() final;
    void OnRemovedFromWorld() final;
    // Steps the simulation (only while the world is started).
    void FixedUpdate( const UpdateContext& inUpdateContext ) final;
    // Keeps bodies in sync with components, writes interpolated poses, debug draws.
    void Update( const UpdateContext& inUpdateContext ) final;

    // Queries. Masks are sets of layers (PhysicsLayers::Bit). Triggers are ignored.
    bool Raycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, RaycastHit& OutHit, uint32_t InLayerMask = PhysicsLayers::All );
    std::vector<RaycastHit> RaycastAll( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, uint32_t InLayerMask = PhysicsLayers::All );
    bool Linecast( const Vector3& InStart, const Vector3& InEnd, RaycastHit& OutHit, uint32_t InLayerMask = PhysicsLayers::All );
    bool SphereCast( const Vector3& InOrigin, float InRadius, const Vector3& InDirection, float InMaxDistance, RaycastHit& OutHit, uint32_t InLayerMask = PhysicsLayers::All );
    std::vector<EntityHandle> OverlapSphere( const Vector3& InCenter, float InRadius, uint32_t InLayerMask = PhysicsLayers::All );
    std::vector<EntityHandle> OverlapBox( const Vector3& InCenter, const Vector3& InHalfExtents, const Quaternion& InRotation, uint32_t InLayerMask = PhysicsLayers::All );
    // Older gameplay code: Raycast from a point towards another point.
    bool Raycast( const Vector3& InStart, const Vector3& InEnd, RaycastHit& OutHit ) { return Linecast( InStart, InEnd, OutHit ); }

    void SetGravity( const Vector3& InGravity );
    Vector3 GetGravity() const;
    bool IsSimulating() const { return m_simulating; }
    size_t GetBodyCount() const { return m_bodies.size(); }
    float GetLastStepMilliseconds() const { return m_lastStepMs; }

    // Draw every body, shape and joint (Scene View > View > Physics, or the inspector).
    static inline bool DebugDrawEnabled = false;

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    struct BodyRecord
    {
        EntityHandle Owner;
        uint64_t Body = 0;
        uint64_t BodySignature = 0;
        uint64_t ShapeSignature = 0;
        std::vector<uint64_t> Shapes;
        std::vector<Moonlight::MeshData*> Meshes;   // cached hull / triangle data in use
        bool HasRigidbody = false;
        bool Seen = false;
        // Pose last applied to / read from the body, and the two fixed-step poses to interpolate.
        Vector3 AppliedPosition;
        Quaternion AppliedRotation;
        Vector3 PreviousPosition;
        Quaternion PreviousRotation;
        Vector3 CurrentPosition;
        Quaternion CurrentRotation;
    };

    struct ColliderEntry
    {
        EntityHandle Entity;
        bool OwnBody = false;   // triangle meshes: never compounded into an ancestor
    };

    // A Box3D task running on the job system.
    struct PhysicsTask
    {
        Jobs::Counter Counter;
        void* Task = nullptr;       // b3TaskCallback*
        void* Context = nullptr;
    };

    void CreateWorld();
    void DestroyWorld();
    void SyncBodies();
    void SyncJoints();
    void BuildShapes( BodyRecord& InRecord, const std::vector<ColliderEntry>& InColliders );
    void ApplyBodySettings( BodyRecord& InRecord, Rigidbody* InRigidbody );
    void DestroyRecord( BodyRecord& InRecord );
    void StepCharacters( float InDeltaSeconds );
    void SyncCharacterBody( CharacterController& InController, Entity& InEntity );
    void ProcessEvents();
    void WritePoses( float InAlpha );
    uint64_t ComputeShapeSignature( Entity& InOwner, const std::vector<ColliderEntry>& InColliders ) const;
    // Pose of a collider entity relative to its body owner (stable: built from local transforms).
    void RelativePose( Transform& InCollider, Transform& InOwner, Vector3& OutPosition, Quaternion& OutRotation ) const;
    const void* GetHull( Moonlight::MeshData* InMesh );
    const void* GetTriangleMesh( Moonlight::MeshData* InMesh );
    uint64_t EnsureGroundBody();
    // Frees cached hull / triangle data no body uses any more.
    void CollectMeshCache();

    static void* EnqueueTask( void* InTask, void* InTaskContext, void* InUserContext, const char* InName );
    static void FinishTask( void* InUserTask, void* InUserContext );

    uint64_t m_world = 0;           // b3WorldId (32 bits used)
    uint64_t m_groundBody = 0;      // static anchor for joints connected to the world
    bool m_simulating = false;
    float m_lastStepMs = 0.f;
    std::unordered_map<uint64_t, BodyRecord> m_bodies;  // by owner entity id
    std::unordered_map<Moonlight::MeshData*, void*> m_hulls;
    std::unordered_map<Moonlight::MeshData*, void*> m_meshes;
    std::array<PhysicsTask, 128> m_tasks;
    int m_taskCount = 0;
    std::vector<CollisionEvent> m_pendingEvents;
};

ME_REGISTER_CORE( PhysicsCore )
