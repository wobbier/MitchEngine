#pragma once
#include "ECS/ComponentDetail.h"
#include "ECS/Component.h"

#if USING( ME_PHYSICS_3D )
#include "btBulletDynamicsCommon.h"
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include "BulletDynamics/Dynamics/btDynamicsWorld.h"
#include "LinearMath/btDefaultMotionState.h"
#include "BulletCollision/CollisionShapes/btCollisionShape.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "LinearMath/btTransform.h"
#endif

class btPairCachingGhostObjectWithEvents;

class CharacterController
    : public Component<CharacterController>
{
    friend class PhysicsCore;
public:
    CharacterController();
    ~CharacterController();

    float Deceleration = 7.f;
    float MaxSpeed = 100.f;
    float JumpForce = 20.f;

    float JumpRechargeTime = 0.5f;

    // Separate init from construction code.
    virtual void Init() final;

#if USING( ME_PHYSICS_3D )
    void Initialize( btDynamicsWorld* pPhysicsWorld, const Vector3 spawnPos, float radius, float height, float mass, float stepHeight );
#endif

    void Walk( const Vector3& direction );
    void Walk( Vector2 direction );

    void Update( const UpdateContext& inUpdateContext );

    void Jump();

    void Teleport( const Vector3& inPosition, const Quaternion& inRotation );

    Vector3 GetPosition() const;
    Vector3 GetVelocity() const;

    bool IsOnGround() const;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() override;
#endif
private:
#if USING( ME_PHYSICS_3D )
    btDynamicsWorld* m_world = nullptr;
    btCollisionShape* m_shape = nullptr;
    btDefaultMotionState* m_motionState = nullptr;
    btRigidBody* m_rigidbody = nullptr;
    btPairCachingGhostObjectWithEvents* m_ghostObject = nullptr;
    btTransform m_motionTransform;
#endif

    bool m_isGrounded = false;
    bool m_isHittingWall = false;
    float m_bottomYOffset = 0.f;
    float m_bottomRoundedRegionYOffset = 0.f;
    float m_stepHeight = 0.1f;


    Vector3 m_manualVelocity;
    std::vector<Vector3> m_surfaceHitNormals;

    btVector3 m_previousPosition;
    float m_jumpTimer = 0.f;

#if USING( ME_PHYSICS_3D )
    void ParseGhostContacts();

    void UpdatePosition();
    void UpdateVelocity( float dt );
#endif

    virtual void OnSerialize( json& outJson ) final;
    virtual void OnDeserialize( const json& inJson ) final;
};

ME_REGISTER_COMPONENT_FOLDER( CharacterController, "Physics" )