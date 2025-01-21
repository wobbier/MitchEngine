#include "PCH.h"
#include "Transform.h"
#include "Engine/Engine.h"
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

Transform::Transform()
    : Component( "Transform" )
    , LocalPosition( 0.f, 0.f, 0.f )
    , LocalScale( 1.0f, 1.0f, 1.0f )
{
}


Transform::Transform( const std::string& TransformName )
    : Component( "Transform" )
    , Name( std::move( TransformName ) )
    , LocalPosition( 0.f, 0.f, 0.f )
    , LocalScale( 1.0f, 1.0f, 1.0f )
{
}

Transform::~Transform()
{
    if( ParentTransform )
    {
        ParentTransform->RemoveChild( shared_from_this().get() );
    }
    ParentTransform = nullptr;
}

void Transform::Init()
{
}

Vector3 Transform::GetPosition() const
{
    return LocalPosition;
}

void Transform::SetPosition( const Vector3& NewPosition )
{
    LocalPosition = NewPosition;
    IsLocalToWorldDirty = true;
}

Quaternion Transform::GetRotation() const
{
    return LocalRotation;
}

void Transform::SetRotation( const Quaternion& InRotation )
{
    LocalRotation = InRotation;
    IsLocalToWorldDirty = true;
}

Vector3 Transform::GetScale()
{
    return LocalScale;
}

void Transform::SetScale( const Vector3& NewScale )
{
    if( NewScale.IsZero() )
    {
        return;
    }
    LocalScale = NewScale;
    IsLocalToWorldDirty = true;
}

void Transform::SetScale( float NewScale )
{
    if( NewScale == LocalScale.x && NewScale == LocalScale.y && NewScale == LocalScale.z )
    {
        return;
    }
    LocalScale = Vector3( NewScale );
    SetDirty( true );
}

Vector3 Transform::GetWorldPosition() const
{
    if( ParentTransform )
    {
        return ParentTransform->GetLocalToWorldMatrix().TransformPoint( GetPosition() );
    }
    return GetPosition();
}

void Transform::SetWorldPosition( const Vector3& NewPosition )
{
    if( ParentTransform )
    {
        SetPosition( ParentTransform->GetWorldToLocalMatrix().TransformPoint( NewPosition ) );
    }
    else
    {
        SetPosition( NewPosition );
    }

    IsWorldToLocalDirty = true;
}

Quaternion Transform::GetWorldRotation()
{
    if( ParentTransform )
    {
        return ParentTransform->GetWorldRotation() * GetRotation();
    }

    return GetRotation();
}

void Transform::SetWorldRotation( const Quaternion& inRotation )
{
    if( ParentTransform )
    {
        SetRotation( ParentTransform->GetWorldRotation().Inverse() * inRotation );
    }
    else
    {
        SetRotation( inRotation );
    }
    IsWorldToLocalDirty = true;
}

void Transform::Translate( Vector3 NewPosition )
{
    if( NewPosition.IsZero() )
    {
        return;
    }
    SetWorldPosition( LocalPosition + NewPosition );
}

void Transform::Rotate( const Vector3& inDegrees, TransformSpace inRelativeTo /*= TransformSpace::Self*/ )
{
    Quaternion eulerRot( inDegrees );
    if( inRelativeTo == TransformSpace::World )
    {
        //LocalRotation = Quaternion(LocalRotation.InternalQuat * (glm::inverse(Rotation.InternalQuat) * eulerRot.InternalQuat * Rotation.InternalQuat));
        //SetRotation(LocalRotation);
    }
    else
    {
        LocalRotation = LocalRotation * eulerRot;
        SetRotation( LocalRotation );
    }
}

Vector3 Transform::Front()
{
    return GetWorldRotation().Rotate( Vector3::Front );
}

Vector3 Transform::Up()
{
    return GetWorldRotation().Rotate( Vector3::Up );
}

Vector3 Transform::Right()
{
    return GetWorldRotation().Rotate( Vector3::Right );
}

void Transform::Reset()
{
    SetWorldPosition( Vector3() );
    SetRotation( Vector3() );
    SetScale( 1.f );
    Matrix4 idMat;
    SetWorldTransform( idMat, true );
}

void Transform::SetWorldTransform( Matrix4& NewWorldTransform, bool InIsDirty )
{
    OPTICK_EVENT( "Transform::SetWorldTransform" );
    {
        //DirectX::SimpleMath::Quaternion quat;
        //DirectX::SimpleMath::Vector3 pos;
        //DirectX::SimpleMath::Vector3 scale;
        //NewWorldTransform.GetInternalMatrix().Decompose(scale, quat, pos);

        //InternalRotation = Quaternion(ParentTransform->GetWorldRotation().GetInternalVec() * Quaternion(quat).GetInternalVec());
        //Vector3 rotation = Quaternion::ToEulerAngles(Quaternion(quat));
        //Rotation = Vector3(Mathf::Degrees(Rotation.X()), Mathf::Degrees(Rotation.Y()), Mathf::Degrees(Rotation.Z()));
        //Position = Vector3(pos);
    }

    // update local transform
    //WorldTransform = std::move(NewWorldTransform);
    IsWorldToLocalDirty = true;
}

const bool Transform::IsDirty() const
{
    return IsLocalToWorldDirty || IsWorldToLocalDirty;
}

const Matrix4& Transform::GetLocalToWorldMatrix()
{
    OPTICK_CATEGORY( "GetLocalToWorldMatrix", Optick::Category::Scene );
    if( FuncIsLocalToWorldDirty() )
    {
        glm::mat4 T = glm::translate( glm::mat4( 1.0f ), GetPosition().InternalVector );
        glm::quat R = GetRotation().InternalQuat;
        glm::mat4 S = glm::scale( glm::mat4( 1.0f ), GetScale().InternalVector );
        glm::mat4 P = ParentTransform ? ParentTransform->GetLocalToWorldMatrix().GetInternalMatrix() : glm::mat4( 1.0f );

        LocalToWorldMatrix = Matrix4( P * T * glm::toMat4( R ) * S );
        IsLocalToWorldDirty = false;
    }

    return LocalToWorldMatrix;
}
bool Transform::FuncIsLocalToWorldDirty()
{
    return IsLocalToWorldDirty || ( ParentTransform && ParentTransform->FuncIsLocalToWorldDirty() );
}

const Matrix4& Transform::GetWorldToLocalMatrix()
{
    OPTICK_CATEGORY( "GetWorldToLocalMatrix", Optick::Category::Scene );
    if( IsWorldToLocalDirty )
    {
        WorldToLocalMatrix = GetLocalToWorldMatrix().Inverse();
        IsWorldToLocalDirty = false;
    }
    return WorldToLocalMatrix;
}

void Transform::SetDirty( bool Dirty )
{
    //OPTICK_EVENT( "Transform::SetDirty" );
    //if( Dirty && ( Dirty != m_isDirty ) )
    //{
    //    for( SharedPtr<Transform>& Child : Children )
    //    {
    //        if( Child )
    //        {
    //            Child->SetDirty( Dirty );
    //        }
    //    }
    //}
    //m_isDirty = Dirty;
}

void Transform::LookAt( const Vector3& InDirection )
{
    //Matrix4 worldMat(glm::transpose(glm::lookAtLH(GetWorldPosition().InternalVector, GetWorldPosition().InternalVector + InDirection.InternalVector, glm::vec3(0,1,0))));
    //SetWorldTransform(worldMat);
    //glm::vec3 scale;
    //glm::quat rot;
    //glm::vec3 pos;
    //glm::vec3 skeq;
    //glm::vec4 poers;
    //glm::decompose(WorldTransform.GetInternalMatrix(), scale, rot, pos, skeq, poers);
    //LocalRotation = Quaternion(rot);
    //Rotation = LocalRotation;
    //Rotation = Vector3(Mathf::Degrees(Rotation.x), Mathf::Degrees(Rotation.y), Mathf::Degrees(Rotation.z));
    //Vector3 worldPos = GetWorldPosition();
    //WorldTransform = Matrix4(DirectX::SimpleMath::Matrix::CreateLookAt(worldPos.InternalVec, (GetWorldPosition() + InDirection).InternalVec, Vector3::Up.InternalVec).Transpose());

    //WorldTransform.GetInternalMatrix()._41 = worldPos[0];
    //WorldTransform.GetInternalMatrix()._42 = worldPos[1];
    //WorldTransform.GetInternalMatrix()._43 = worldPos[2];

    //DirectX::SimpleMath::Quaternion quat;
    //WorldTransform.GetInternalMatrix().Decompose(DirectX::SimpleMath::Vector3(), quat, DirectX::SimpleMath::Vector3());

    //Quaternion quat2(quat);

    //Rotation = Quaternion::ToEulerAngles(quat2);
    //Rotation = Vector3(Mathf::Degrees(Rotation.x), Mathf::Degrees(Rotation.y), Mathf::Degrees(Rotation.z));
    //LocalRotation = quat2;
    ME_ASSERT_MSG( false, "Unused Transform::LookAt" );
    SetDirty( true );
}

void Transform::SetRotation( const Vector3& euler )
{
    LocalRotation.InternalQuat = glm::quat( glm::vec3( Mathf::Radians( euler.x ), Mathf::Radians( euler.y ), Mathf::Radians( euler.z ) ) );
    SetDirty( true );
}

Vector3 Transform::GetRotationEuler() const
{
    return Quaternion::ToEulerAngles( LocalRotation );
}

Vector3 Transform::GetWorldRotationEuler()
{
    return Quaternion::ToEulerAngles( GetWorldRotation() );
}

void Transform::SetParent( Transform& NewParent )
{
    if( ParentTransform )
    {
        GetParentTransform()->RemoveChild( this );
    }

    ParentTransform = NewParent.shared_from_this();
    GetParentTransform()->Children.push_back( shared_from_this() );
    SetDirty( true );
}

void Transform::RemoveChild( Transform* TargetTransform )
{
    if( Children.size() > 0 )
    {
        Children.erase( std::remove( Children.begin(), Children.end(), TargetTransform->shared_from_this() ), Children.end() );
        TargetTransform->ParentTransform = nullptr;
    }
}

Transform* Transform::GetChildByName( const std::string& inName )
{
    for( auto child : Children )
    {
        if( child->Name == inName )
        {
            return child.get();
        }
    }
    return nullptr;
}

const std::vector<SharedPtr<Transform>>& Transform::GetChildren() const
{
    return Children;
}

Transform* Transform::GetParentTransform()
{
    if( ParentTransform )
    {
        return ParentTransform.get();
    }
    return nullptr;
}

const Matrix4& Transform::GetMatrix()
{
    return GetLocalToWorldMatrix();
}

const std::string& Transform::GetName() const
{
    return Name;
}

void Transform::OnSerialize( json& outJson )
{
    outJson["Position"] = { LocalPosition.x, LocalPosition.y, LocalPosition.z };
    Vector3 outRot = Quaternion::ToEulerAngles( LocalRotation );
    outJson["Rotation"] = { outRot.x, outRot.y, outRot.z };
    outJson["Scale"] = { LocalScale.x, LocalScale.y, LocalScale.z };
}

void Transform::OnDeserialize( const json& inJson )
{
    SetPosition( Vector3( (float)inJson["Position"][0], (float)inJson["Position"][1], (float)inJson["Position"][2] ) );
    SetRotation( Vector3( (float)inJson["Rotation"][0], (float)inJson["Rotation"][1], (float)inJson["Rotation"][2] ) );
    if( inJson.find( "Scale" ) != inJson.end() )
    {
        SetScale( Vector3( (float)inJson["Scale"][0], (float)inJson["Scale"][1], (float)inJson["Scale"][2] ) );
    }
}

void Transform::SetName( const std::string& name )
{
    Name = name;
}

#if USING( ME_EDITOR )

void Transform::OnEditorInspect()
{
    HavanaUtils::Label( "Name" );
    ImGui::InputText( "##Name", &Name );

    Vector3 OldPosition = LocalPosition;
    if( HavanaUtils::EditableVector3( "Local Position", OldPosition ) )
    {
        SetPosition( OldPosition );
    }

    Vector3 OldRotation = Quaternion::ToEulerAngles( GetRotation() );
    if( HavanaUtils::EditableVector3( "Local Rotation", OldRotation ) )
    {
        SetRotation( OldRotation );
    }

    Vector3 OldScale = LocalScale;
    if( HavanaUtils::EditableVector3( "Scale", OldScale, 1.f ) )
    {
        SetScale( OldScale );
    }

    Vector3 WorldPos = GetWorldPosition();
    if( HavanaUtils::EditableVector3( "World Position", WorldPos ) )
    {
        SetWorldPosition( WorldPos );
    }

    Vector3 OldWorldRotation = GetWorldRotationEuler();
    if( HavanaUtils::EditableVector3( "World Rotation", OldWorldRotation ) )
    {
        //SetWorldRotation(OldRotation);
    }

    if( ImGui::Button( "Reset Transform" ) )
    {
        Reset();
    }

// 	ImGui::Text(GetLocalToWorldMatrix().ToString().c_str());
// 	ImGui::Separator();
// 	ImGui::Text(GetWorldToLocalMatrix().ToString().c_str());
}

#endif