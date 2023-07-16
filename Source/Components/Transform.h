#pragma once
#include "ECS/Component.h"

#include "Dementia.h"
#include "Math/Vector3.h"
#include "ECS/ComponentDetail.h"
#include "Utils/HavanaUtils.h"
#include "Math/Quaternion.h"
#include "Math/Matrix4.h"

enum class TransformSpace : uint8_t
{
    Self = 0,
    World
};

class Transform
    : public Component<Transform>
    , public std::enable_shared_from_this<Transform>
{
    typedef Component<Transform> Base;
    friend class SceneCore;
public:

    Transform();
    Transform( const std::string& Name );
    virtual ~Transform();

    // Separate init from construction code.
    virtual void Init() final;

    // Local Space
    Vector3 GetPosition() const;
    void SetPosition( const Vector3& NewPosition );

    Quaternion GetRotation() const;
    void SetRotation( const Quaternion& InRotation );

    Vector3 GetScale();
    void SetScale( const Vector3& NewScale );
    void SetScale( float NewScale );

    //World Space
    Vector3 GetWorldPosition() const;
    void SetWorldPosition( const Vector3& NewPosition );

    Quaternion GetWorldRotation();
    void SetWorldRotation( const Quaternion& inRotation );

    // Euler
    Vector3 GetRotationEuler() const;
    Vector3 GetWorldRotationEuler();

    void SetRotation( const Vector3& euler );

    Vector3 Front();
    Vector3 Up();
    Vector3 Right();

    void LookAt( const Vector3& InDirection );
    void Translate( Vector3 NewTransform );

    void Rotate( const Vector3& inDegrees, TransformSpace inRelativeTo = TransformSpace::Self );

    void Reset();

    ME_HARDSTUCK( Transform )

    void SetParent( Transform& NewParent );
    void RemoveChild( Transform* TargetTransform );
    Transform* GetChildByName( const std::string& inName );
    const std::vector<SharedPtr<Transform>>& GetChildren() const;

    Transform* GetParentTransform();
    const Matrix4& GetMatrix();

    const std::string& GetName() const;
    void SetName( const std::string& name );
    void SetWorldTransform( Matrix4& NewWorldTransform, bool InIsDirty = false );

    const bool IsDirty() const;

    const Matrix4& GetLocalToWorldMatrix();
    const Matrix4& GetWorldToLocalMatrix();


    virtual void OnEditorInspect() final;

private:
    std::string Name;

    Quaternion LocalRotation;
    Vector3 LocalPosition;
    Vector3 LocalScale;

    Matrix4 LocalToWorldMatrix;
    Matrix4 WorldToLocalMatrix;

    bool IsLocalToWorldDirty = true;
    bool IsWorldToLocalDirty = true;

    SharedPtr<Transform> ParentTransform;
    std::vector<SharedPtr<Transform>> Children;
    bool m_isDirty = true;

    void SetDirty( bool Dirty );
    virtual void OnSerialize( json& outJson ) final;
    virtual void OnDeserialize( const json& inJson ) final;
};

ME_REGISTER_COMPONENT( Transform )
