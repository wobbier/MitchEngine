#pragma once
#include "ECS/Core.h"
#include "ECS/CoreDetail.h"
#include "Jobs/JobSystem.h"
#include "Math/Vector2.h"
#include "Physics/PhysicsTypes.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class Transform;
class Rigidbody2D;
class CharacterController2D;

// 2D physics (Box2D) in the world XY plane. Engine-owned, and the counterpart of PhysicsCore:
// builds bodies from Rigidbody2D / 2D collider / CharacterController2D / PhysicsJoint2D components,
// steps at the fixed rate while the world is started, interpolates the poses it writes and keeps
// bodies on their Transforms outside play mode.
//
// - Bodies own a Transform's X/Y position and its rotation about world Z; its Z position and any
//   X/Y tilt are kept (a wheel modelled as a cylinder turned on its side stays turned).
// - Colliders attach to the entity's Rigidbody2D, or the nearest ancestor's; with neither they make
//   a static body.
// - The same entity layers and Project Settings collision matrix as 3D; gravity is its X/Y.
// - CollisionEvent (Is2D = true) fires after each step for contacts and triggers.
class Physics2DCore final
    : public Core<Physics2DCore>
{
public:
    Physics2DCore();
    ~Physics2DCore() override;

    void Init() final;
    void OnStart() final;
    void OnStop() final;
    void OnRemovedFromWorld() final;
    void FixedUpdate( const UpdateContext& inUpdateContext ) final;
    void Update( const UpdateContext& inUpdateContext ) final;
    // Brings bodies in line with components now, so entities spawned this frame can be queried
    // before the next step (scripts' OnStart).
    void SyncNow();

    // Queries in the XY plane. Masks are sets of layers (PhysicsLayers::Bit). Triggers are ignored.
    bool Raycast( const Vector2& InOrigin, const Vector2& InDirection, float InMaxDistance, RaycastHit& OutHit, uint32_t InLayerMask = PhysicsLayers::All );
    std::vector<RaycastHit> RaycastAll( const Vector2& InOrigin, const Vector2& InDirection, float InMaxDistance, uint32_t InLayerMask = PhysicsLayers::All );
    bool Linecast( const Vector2& InStart, const Vector2& InEnd, RaycastHit& OutHit, uint32_t InLayerMask = PhysicsLayers::All );
    bool CircleCast( const Vector2& InOrigin, float InRadius, const Vector2& InDirection, float InMaxDistance, RaycastHit& OutHit, uint32_t InLayerMask = PhysicsLayers::All );
    std::vector<EntityHandle> OverlapCircle( const Vector2& InCenter, float InRadius, uint32_t InLayerMask = PhysicsLayers::All );
    std::vector<EntityHandle> OverlapBox( const Vector2& InCenter, const Vector2& InHalfExtents, float InAngleDegrees, uint32_t InLayerMask = PhysicsLayers::All );
    std::vector<EntityHandle> OverlapPoint( const Vector2& InPoint, uint32_t InLayerMask = PhysicsLayers::All );

    void SetGravity( const Vector2& InGravity );
    Vector2 GetGravity() const;
    bool IsSimulating() const { return m_simulating; }
    size_t GetBodyCount() const { return m_bodies.size(); }
    float GetLastStepMilliseconds() const { return m_lastStepMs; }

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
        bool Seen = false;
        // Pose last applied to / read from the body, and the two fixed-step poses to interpolate.
        Vector2 AppliedPosition;
        float AppliedAngle = 0.f;
        Vector2 PreviousPosition;
        float PreviousAngle = 0.f;
        Vector2 CurrentPosition;
        float CurrentAngle = 0.f;
    };

    // A Box2D parallel-for running on the job system.
    struct PhysicsTask
    {
        Jobs::Counter Counter;
        void* Task = nullptr;       // b2TaskCallback*
        void* Context = nullptr;
    };

    void CreateWorld();
    void DestroyWorld();
    void SyncBodies();
    void SyncJoints();
    void BreakJoints();
    void BuildShapes( BodyRecord& InRecord, const std::vector<EntityHandle>& InColliders );
    void ApplyBodySettings( BodyRecord& InRecord, Rigidbody2D* InRigidbody );
    void DestroyRecord( BodyRecord& InRecord );
    void StepCharacters( float InDeltaSeconds );
    void SyncCharacterBody( CharacterController2D& InController, Entity& InEntity );
    void ProcessEvents();
    void WritePoses( float InAlpha );
    uint64_t ComputeShapeSignature( Entity& InOwner, const std::vector<EntityHandle>& InColliders ) const;
    uint64_t EnsureGroundBody();

    static void* EnqueueTask( void* InTask, int InItemCount, int InMinRange, void* InTaskContext, void* InUserContext );
    static void FinishTask( void* InUserTask, void* InUserContext );

    uint32_t m_world = 0;           // b2WorldId
    uint64_t m_groundBody = 0;      // static anchor for joints connected to the world
    bool m_simulating = false;
    float m_lastStepMs = 0.f;
    uint32_t m_nextCharacterSlot = 0;
    std::unordered_map<uint64_t, BodyRecord> m_bodies;  // by owner entity id
    std::array<PhysicsTask, 128> m_tasks;
    int m_taskCount = 0;
    std::vector<CollisionEvent> m_pendingEvents;
};

ME_REGISTER_CORE( Physics2DCore )
