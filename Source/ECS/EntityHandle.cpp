#include "PCH.h"

#include "Entity.h"
#include "Engine/World.h"

EntityHandle::EntityHandle(EntityID InID, World* InWorld)
	: ID(InID)
	, GameWorld(InWorld)
{
}


EntityHandle::operator bool() const
{
	return GameWorld && GameWorld->EntityExists(ID);
}

Entity* EntityHandle::operator->() const
{
	return Get();
}

Entity* EntityHandle::Get() const
{
	return GameWorld->GetEntityRaw(ID);
}
