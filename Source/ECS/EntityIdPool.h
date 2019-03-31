#pragma once
#include "Dementia.h"
#include "ECS/Entity.h"
#include "EntityID.h"

class EntityIdPool
{
public:
	EntityIdPool(std::size_t InPoolSize);

	ME_NONCOPYABLE(EntityIdPool);
	ME_NONMOVABLE(EntityIdPool);

	EntityID Create();

	std::size_t GetSize() const;

	void Resize(std::size_t InAmount);

	void Remove(EntityID InEntityId);
protected:
private:
	std::size_t DefaultPoolSize;

	std::size_t NextId;

	std::vector<EntityID> FreeList;

	std::vector<EntityID::IntType> Entities;
};
