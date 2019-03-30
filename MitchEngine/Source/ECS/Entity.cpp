#include "PCH.h"
#include "Entity.h"
#include "Engine/World.h"

Entity::Entity()
{
}

Entity::Entity(World& inWorld, ID inId) :
	Id(inId),
	GameWorld(&inWorld)
{
}

Entity::~Entity()
{
}

const bool Entity::HasComponent(TypeId inComponentType) const
{
	ComponentTypeArray ar = GameWorld->EntityAttributes.Storage.GetComponentTypes(*this);

	return ar[inComponentType] == true;
}

void Entity::AddComponent(BaseComponent* inComponent, TypeId inComponentTypeId)
{
	GameWorld->EntityAttributes.Storage.AddComponent(*this, inComponent, inComponentTypeId);
	SetActive(true);
}

const Entity::ID& Entity::GetId() const
{
	return Id;
}

BaseComponent& Entity::GetComponent(TypeId InTypeId) const
{
	return GameWorld->EntityAttributes.Storage.GetComponent(*this, InTypeId);
}

void Entity::SetActive(const bool InActive)
{
	GameWorld->ActivateEntity(*this, InActive);
}

bool Entity::operator==(const Entity& entity) const
{
	return Id == entity.Id && entity.GameWorld == GameWorld;
}

void Entity::RemoveComponent(TypeId InComponentTypeId)
{
	GameWorld->EntityAttributes.Storage.RemoveComponent(*this, InComponentTypeId);
}