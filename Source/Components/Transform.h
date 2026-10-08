#pragma once
#include "Dementia.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"

#include "Math/Matrix4.h"
#include <vector>

enum class TransformSpace : uint8_t
{
    Self = 0,
    World
};

// Position/rotation/scale relative to the parent transform, plus the scene hierarchy.
//
// World matrices are cached and recomputed lazily. Invariant: when a transform is dirty, all of its
// descendants are dirty too, so marking stops early at already-dirty subtrees. Transform::UpdateAll
// resolves every dirty matrix on the main thread before render/physics jobs read them concurrently.
class Transform
    : public Component<Transform>
{
    ME_REFLECTABLE( Transform )
    typedef Component<Transform> Base;
    friend class SceneCore;
    friend class World;
public:

    Transform();
    Transform( const std::string& Name );
    virtual ~Transform();

    virtual void Init() final;
    virtual void OnDestroy() final;
    virtual void OnPropertyChanged( const std::string& InFieldName ) final;

    // Local space
    const Vector3& GetPosition() const;
    void SetPosition( const Vector3& NewPosition );

    const Quaternion& GetRotation() const;
    void SetRotation( const Quaternion& InRotation );
    // Euler angles in degrees.
    void SetRotation( const Vector3& euler );
    Vector3 GetRotationEuler() const;

    const Vector3& GetScale() const;
    void SetScale( const Vector3& NewScale );
    void SetScale( float NewScale );

    // World space
    Vector3 GetWorldPosition();
    void SetWorldPosition( const Vector3& NewPosition );

    Quaternion GetWorldRotation();
    void SetWorldRotation( const Quaternion& inRotation );
    Vector3 GetWorldRotationEuler();

    // Approximate world scale (exact unless a parent is non-uniformly scaled and rotated).
    Vector3 GetWorldScale();

    // Replaces the local transform so the world transform equals NewWorld.
    void SetWorldMatrix( const Matrix4& NewWorld );

    // World-space basis vectors.
    Vector3 Front();
    Vector3 Up();
    Vector3 Right();

    // Rotates so Front() points at a world-space target / along a world-space direction.
    void LookAt( const Vector3& InWorldTarget, const Vector3& InUp = Vector3::Up );
    void LookDirection( const Vector3& InDirection, const Vector3& InUp = Vector3::Up );

    // Moves by a delta expressed in world space (default) or this transform's local axes.
    void Translate( const Vector3& InDelta, TransformSpace InSpace = TransformSpace::World );

    // Rotates by Euler degrees around this transform's axes or the world axes.
    void Rotate( const Vector3& inDegrees, TransformSpace inRelativeTo = TransformSpace::Self );

    Vector3 TransformPoint( const Vector3& InLocalPoint );
    Vector3 TransformDirection( const Vector3& InLocalDirection );
    Vector3 InverseTransformPoint( const Vector3& InWorldPoint );
    Vector3 InverseTransformDirection( const Vector3& InWorldDirection );

    void Reset();

    ME_HARDSTUCK( Transform )

    // Hierarchy. keepWorldTransform preserves the current world pose under the new parent.
    void SetParent( Transform& NewParent, bool keepWorldTransform = false );
    void DetachFromParent( bool keepWorldTransform = false );
    void RemoveChild( Transform* TargetTransform );
    Transform* GetParentTransform() const;
    const std::vector<Transform*>& GetChildren() const;
    Transform* GetChildByName( const std::string& inName ) const;
    Transform* FindDescendantByName( const std::string& inName ) const;
    bool IsDescendantOf( const Transform& InAncestor ) const;
    size_t GetSiblingIndex() const;
    void SetSiblingIndex( size_t InIndex );

    // Matrices
    const Matrix4& GetMatrix();
    const Matrix4& GetLocalToWorldMatrix();
    const Matrix4& GetWorldToLocalMatrix();
    Matrix4 GetLocalMatrix() const;
    bool IsDirty() const;

    // Resolves every dirty world matrix in the world (main thread). Call before jobs read matrices.
    static void UpdateAll( class World& InWorld );

    // The entity's name (stored on the entity; kept here for convenience).
    const std::string& GetName() const;
    void SetName( const std::string& name );

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

private:
    void MarkDirty();

    Vector3 LocalPosition;
    Quaternion LocalRotation;
    Vector3 LocalScale;

    Matrix4 LocalToWorldMatrix;
    Matrix4 WorldToLocalMatrix;
    bool m_isWorldDirty = true;
    bool m_isInverseDirty = true;

    Transform* m_parent = nullptr;
    std::vector<Transform*> m_children;

    // Name given to the constructor before the owning entity is known.
    std::string m_pendingName;

    virtual void OnSerialize( json& outJson ) final;
    virtual void OnDeserialize( const json& inJson ) final;
};

// Weak reference to a Transform via its owning entity; resolves to null once the entity or the
// component is destroyed. lock() mirrors the old WeakPtr<Transform> API used by editor widgets.
class TransformHandle
{
public:
    struct Locked
    {
        Transform* Ptr = nullptr;
        Transform* operator->() const { return Ptr; }
        Transform& operator*() const { return *Ptr; }
        Transform* get() const { return Ptr; }
        explicit operator bool() const { return Ptr != nullptr; }
    };

    TransformHandle() = default;
    TransformHandle( Transform* InTransform );
    TransformHandle( Transform& InTransform ) : TransformHandle( &InTransform ) {}

    Transform* Get() const;
    Locked lock() const { return Locked{ Get() }; }
    void reset() { m_entity.Reset(); }
    explicit operator bool() const { return Get() != nullptr; }
    bool operator==( const TransformHandle& InOther ) const { return m_entity == InOther.m_entity; }

private:
    EntityHandle m_entity;
};

ME_REGISTER_COMPONENT( Transform )
