#include "PCH.h"
#include "Transform.h"
#include <algorithm>
#include <gtc/matrix_transform.hpp>

Transform::Transform()
	: LocalTransform(1.f)
	, WorldTransform(1.f)
{
}


Transform::Transform(const std::string& TransformName)
	: LocalTransform(1.f)
	, WorldTransform(1.f)
	, Name(std::move(TransformName))
{

}

Transform::~Transform()
{
}


void Transform::SetPosition(glm::vec3 NewPosition)
{
	LocalTransform[3] = glm::vec4(NewPosition, 1.f);
	SetDirty(true);
}


void Transform::SetScale(glm::vec3 NewScale)
{
	LocalTransform[0][0] = NewScale.x;
	LocalTransform[1][1] = NewScale.y;
	LocalTransform[2][2] = NewScale.z;
	SetDirty(true);
}

void Transform::Translate(glm::vec3 NewPosition)
{
	LocalTransform = glm::translate(LocalTransform, NewPosition);
	SetDirty(true);
}

glm::vec3 Transform::GetPosition()
{
	return glm::vec3(LocalTransform[3][0], LocalTransform[3][1], LocalTransform[3][2]);
}

void Transform::SetWorldTransform(glm::mat4& NewWorldTransform)
{
	// update local transform
	WorldTransform = NewWorldTransform;
	SetDirty(false);
}

void Transform::Init()
{
}

void Transform::SetDirty(bool Dirty)
{
	if (Dirty && (Dirty != IsDirty) && Children.size())
	{
		for (auto Child : Children)
		{
			Child->SetDirty(Dirty);
		}
	}
	IsDirty = Dirty;
}

void Transform::SetParent(Transform& NewParent)
{
	if (ParentTransform)
	{
		ParentTransform->RemoveChild(this);
	}
	ParentTransform = &NewParent;
	ParentTransform->Children.push_back(this);
	SetDirty(true);
}

void Transform::RemoveChild(Transform* TargetTransform)
{
	Children.erase(std::remove(Children.begin(), Children.end(), TargetTransform), Children.end());
}

DirectX::XMMATRIX Transform::GetMatrix()
{
	DirectX::XMMATRIX mat = DirectX::XMMATRIX(
		WorldTransform[0][0], WorldTransform[0][1], WorldTransform[0][2], WorldTransform[0][3],
		WorldTransform[1][0], WorldTransform[1][1], WorldTransform[1][2], WorldTransform[1][3],
		WorldTransform[2][0], WorldTransform[2][1], WorldTransform[2][2], WorldTransform[2][3],
		WorldTransform[3][0], WorldTransform[3][1], WorldTransform[3][2], WorldTransform[3][3]);
	//DirectX::XMMatrixIdentity();
	return mat;
}
