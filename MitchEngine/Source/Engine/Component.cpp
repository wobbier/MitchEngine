#include "Component.h"
#include "Entity.h"

const Entity* BaseComponent::GetParentEntity()
{
	return ParentEntity;
}

void BaseComponent::OnCreate(Entity* Owner)
{
	ParentEntity = Owner;
}
