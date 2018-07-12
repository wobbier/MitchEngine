#include "Transform.h"
#include <algorithm>
#include <gtc/matrix_transform.hpp>

Transform::Transform()
	: LocalTransform(1.f)
	, WorldTransform(1.f)
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
