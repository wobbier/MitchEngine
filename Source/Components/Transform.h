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
	friend class SceneGraph;
public:

	Transform();
	Transform(const std::string& Name);
	virtual ~Transform();

	// Separate init from construction code.
	virtual void Init() final;

	// Local Space
	Vector3& GetPosition();
	Quaternion GetLocalRotation() const;
	Vector3 GetRotationEuler() const;
	Vector3 GetScale();

	void SetPosition(Vector3 NewPosition);

	void SetScale(Vector3 NewScale);
	void SetScale(float NewScale);

	//World Space
	Vector3 GetWorldPosition();
	Quaternion GetWorldRotation();
	Vector3 GetWorldRotationEuler();

	void SetWorldPosition(const Vector3& NewPosition);
	void SetRotation(const Vector3& euler);
	void SetRotation(Quaternion InRotation);

	Vector3 Front();
	Vector3 Up();
	Vector3 Right();

	void LookAt(const Vector3& InDirection);
	void Translate(Vector3 NewTransform);

	void Rotate(const Vector3& inDegrees, TransformSpace inRelativeTo = TransformSpace::Self);

	void UpdateRecursively(SharedPtr<Transform> CurrentTransform);
	void UpdateWorldTransform();

	void Reset();

	ME_NONCOPYABLE(Transform)
	ME_NONMOVABLE(Transform)

	void SetParent(Transform& NewParent);
	void RemoveChild(Transform* TargetTransform);
	Transform* GetChildByName(const std::string& Name);
	const std::vector<SharedPtr<Transform>>& GetChildren() const;

	Transform* GetParentTransform();
	Matrix4& GetMatrix();

	const std::string& GetName() const;
	void SetName(const std::string& name);
	void SetWorldTransform(Matrix4& NewWorldTransform, bool InIsDirty = false);

	const bool IsDirty() const;

	Matrix4 GetLocalToWorldMatrix();
	Matrix4 GetWorldToLocalMatrix();


//#if ME_EDITOR
	virtual void OnEditorInspect() final;
//#endif

private:
	std::string Name;
	Quaternion LocalRotation;
	Quaternion Rotation;
	Vector3 LocalPosition;
	Vector3 Scale;

	Matrix4 WorldTransform;

	void SetDirty(bool Dirty);
	bool m_isDirty = true;
	SharedPtr<Transform> ParentTransform;
	std::vector<SharedPtr<Transform>> Children;

	virtual void OnSerialize(json& outJson) final;
	virtual void OnDeserialize(const json& inJson) final;
};
ME_REGISTER_COMPONENT(Transform)
