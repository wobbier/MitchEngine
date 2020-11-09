#pragma once
#include "ECS/Component.h"

#include "Dementia.h"
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "ECS/ComponentDetail.h"
#include "Utils/HavanaUtils.h"
#include "Math/Quaternion.h"
#include "Math/Matrix4.h"

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

	// Local Space
	void SetPosition(Vector3 NewPosition);
	Vector3& GetPosition();

	void UpdateRecursively(SharedPtr<Transform> CurrentTransform);

	void UpdateWorldTransform();

	void SetRotation(const Vector3& euler);
	void SetRotation(Quaternion InRotation);
	Vector3 GetRotationEuler() const;

	Quaternion GetWorldRotation();
	Vector3 GetWorldRotationEuler();
	//void SetRotation(glm::quat quat);
	Vector3 Front();
	Vector3 Up();

	void SetScale(Vector3 NewScale);
	void SetScale(float NewScale);
	Vector3 GetScale();

	void LookAt(const Vector3& InDirection);

	void Translate(Vector3 NewTransform);

	//World Space
	Vector3 GetWorldPosition();
	void SetWorldPosition(const Vector3& NewPosition);

	void Reset();

	ME_NONCOPYABLE(Transform)
	ME_NONMOVABLE(Transform)

	// Separate init from construction code.
	virtual void Init() final;

	void SetParent(Transform& NewParent);
	void RemoveChild(Transform* TargetTransform);
	Transform* GetChildByName(const std::string& Name);
	const std::vector<SharedPtr<Transform>>& GetChildren() const;

	Transform* GetParentTransform();

	Matrix4 WorldTransform;
	std::string Name;
	Matrix4 GetMatrix();

	void SetName(const std::string& name);
	void SetWorldTransform(Matrix4& NewWorldTransform, bool InIsDirty = false);

	Quaternion LocalRotation;

	const bool IsDirty() const;

	Transform* GetParent();

	Matrix4 GetLocalToWorldMatrix();
	Matrix4 GetWorldToLocalMatrix();


#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif

private:
	Vector3 Rotation;
	Vector3 Position;
	Vector3 Scale;

	void SetDirty(bool Dirty);
	bool m_isDirty = true;
	SharedPtr<Transform> ParentTransform;
	std::vector<SharedPtr<Transform>> Children;

	virtual void OnSerialize(json& outJson) final;
	virtual void OnDeserialize(const json& inJson) final;
};
ME_REGISTER_COMPONENT(Transform)
