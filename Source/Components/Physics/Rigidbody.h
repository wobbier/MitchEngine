#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#if USING( ME_PHYSICS_3D )
#include <btBulletDynamicsCommon.h>
#endif

class btRigidBodyWithEvents;
class btDiscreteDynamicsWorld;

class Rigidbody
    : public Component<Rigidbody>
{
    friend class PhysicsCore;
    friend class SceneViewWidget;
public:

    enum class ColliderType : unsigned int
    {
        Box = 0,
        Sphere,
        Count
    };
    Rigidbody();
    Rigidbody( ColliderType type );
    virtual ~Rigidbody() final;

    // Separate init from construction code.
    virtual void Init() final;

    bool IsRigidbodyInitialized();
    void ApplyForce( const Vector3& direction, float force );
    void SetScale( Vector3 InScale );
    const Vector3& GetScale() const;
    void SetMass( float InMass );
    void SetVelocity( Vector3 newVelocity );

    const bool IsDynamic() const;

    std::string GetColliderString( ColliderType InType );

    Matrix4 GetMat();

    void SetReceiveEvents( bool inIsEventsEnabled );

    unsigned int Id = 0;

    std::vector<Rigidbody*> NewCollisions;
private:
#if USING( ME_PHYSICS_3D )
    void CreateObject( const Vector3& Position, const Quaternion& Rotation, const Vector3& InScale, class btDiscreteDynamicsWorld* world );
    btRigidBodyWithEvents* InternalRigidbody = nullptr;
    btCollisionShape* fallShape = nullptr;
#endif
    Vector3 Scale;
    Vector3 Velocity;
    ColliderType Type = ColliderType::Box;
    float Mass = 10.0f;
    bool m_isDynamic = false;
    bool IsEventsEnabled = false;

    virtual void OnSerialize( json& outJson ) final;
    virtual void OnDeserialize( const json& inJson ) final;
protected:
    bool IsInitialized = false;
    btDiscreteDynamicsWorld* m_world = nullptr;
    int DebugColliderId = 0;
#if USING( ME_EDITOR )

    virtual void OnEditorInspect() final;

#endif
};

ME_REGISTER_COMPONENT_FOLDER( Rigidbody, "Physics" )
