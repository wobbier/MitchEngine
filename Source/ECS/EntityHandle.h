#pragma once
#include "EntityID.h"
#include <type_traits>

class World;
class Entity;

struct EntityIDHash
{
	std::size_t operator()(const EntityID& k) const
	{
		return std::hash<EntityID::IntType>()(k.Value());
	}
};

class EntityHandle
{
public:
	EntityHandle() = default;
	EntityHandle(EntityID InID, World* InWorld);

	explicit operator bool() const;
	Entity* operator->() const;

	Entity* Get() const;

private:
	EntityID ID;
	World* GameWorld = nullptr;
};
