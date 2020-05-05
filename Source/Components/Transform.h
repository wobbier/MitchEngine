#pragma once
#include "ECS/Component.h"

#define GLM_ENABLE_EXPERIMENTAL 1
#include "Dementia.h"
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "ECS/ComponentDetail.h"
#include "Utils/HavanaUtils.h"
#include "Math/Quaternion.h"
#include "Math/Matirx4.h"

class Transform :
	public Component<Transform>
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

	void UpdateRecursively(Transform* CurrentTransform);

	void UpdateWorldTransform();

	void SetRotation(Vector3 euler);
	void SetWorldRotation(Quaternion InRotation);
	Vector3 GetRotation();
	Quaternion GetWorldRotation();
	Vector3 GetWorldRotationEuler();
	//void SetRotation(glm::quat quat);
	Vector3 Front();

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
	std::vector<Transform*> GetChildren() const;

	Transform* GetParentTransform();

	Matrix4 WorldTransform;
	std::string Name;
	Matrix4 GetMatrix();

	virtual void Serialize(json& outJson) final;

	virtual void Deserialize(const json& inJson) final;
	
	void SetName(const std::string& name);
	void SetWorldTransform(Matrix4& NewWorldTransform, bool InIsDirty = false);

	Quaternion InternalRotation;

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
	EntityHandle ParentTransform;
	std::vector<EntityHandle> Children;
};
ME_REGISTER_COMPONENT(Transform)
