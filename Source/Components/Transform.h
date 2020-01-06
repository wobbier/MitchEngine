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
	friend class Havana;
	friend class Scene;
	friend class EditorCore;
	friend class SceneGraph;
	friend class PhysicsCore;
public:
	Transform();
	Transform(const std::string& Name);
	virtual ~Transform();

	// Local Space
	void SetPosition(Vector3 NewPosition);
	Vector3& GetPosition();

	void UpdateRecursively(Transform* CurrentTransform);

	void SetRotation(Vector3 euler);
	Vector3 GetRotation();
	//void SetRotation(glm::quat quat);

	void SetScale(Vector3 NewScale);
	void SetScale(float NewScale);
	Vector3 GetScale();

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
	std::vector<Transform*> GetChildren() const
	{
		return Children;
	}

	Matrix4 WorldTransform;
	std::string Name;
	Matrix4 GetMatrix();

	virtual void Serialize(json& outJson) final;

	virtual void Deserialize(const json& inJson) final;
	
	void SetName(const std::string& name);
	void SetWorldTransform(Matrix4& NewWorldTransform);

private:
	Vector3 Position;
	Vector3 Scale;
	Vector3 Rotation;
#if ME_EDITOR
	virtual void OnEditorInspect() final;

#endif

	void SetDirty(bool Dirty);
	bool IsDirty = true;
	Transform* ParentTransform = nullptr;
	std::vector<Transform*> Children;
};
ME_REGISTER_COMPONENT(Transform)
