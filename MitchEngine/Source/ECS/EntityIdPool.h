#pragma once
#include "Utility/Util.h"
#include "ECS/Entity.h"

class EntityIdPool
{
public:
	EntityIdPool(std::size_t InPoolSize);

	MAN_NONCOPYABLE(EntityIdPool);
	MAN_NONMOVABLE(EntityIdPool);

	Entity::ID Create();

	std::size_t GetSize() const;

	void Resize(std::size_t InAmount);

	void Remove(Entity::ID InEntityId);
protected:
private:
	std::size_t DefaultPoolSize;

	std::size_t NextId;

	std::vector<Entity::ID> FreeList;

	std::vector<Entity::ID::IntType> Entities;
};
