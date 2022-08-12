#include "PCH.h"

#include "Entity.h"
#include "Engine/World.h"

EntityHandle::EntityHandle(EntityID InID, WeakPtr<World> InWorld)
	: ID(InID)
	, GameWorld(InWorld)
{
}


EntityHandle::operator bool() const
{
	return GameWorld.lock() && GameWorld.lock()->EntityExists(ID);
}

bool EntityHandle::operator==(const EntityHandle& other) const
{
	return ID == other.ID;
}

Entity* EntityHandle::operator->() const
{
	return Get();
}

Entity* EntityHandle::Get() const
{
	return GameWorld.lock() ? GameWorld.lock()->GetEntityRaw(ID) : nullptr;
}

EntityID EntityHandle::GetID() const
{
	return ID;
}
