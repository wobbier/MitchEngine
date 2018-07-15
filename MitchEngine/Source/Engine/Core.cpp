#include "PCH.h"
#include "Core.h"

BaseCore::BaseCore(const ComponentFilter& Filter) : CompFilter(Filter)
{
}


void BaseCore::OnEntityAdded(Entity& NewEntity)
{
}

World& BaseCore::GetWorld() const
{
	return *GameWorld;
}

std::vector<Entity> BaseCore::GetEntities() const
{
	return Entities;
}

const ComponentFilter& BaseCore::GetComponentFilter() const
{
	return CompFilter;
}

void BaseCore::Add(Entity& InEntity)
{
	Entities.push_back(InEntity);
	OnEntityAdded(InEntity);
}

BaseCore::~BaseCore()
{
}

void BaseCore::Remove(Entity& InEntity)
{
	Entities.erase(std::remove(Entities.begin(), Entities.end(), InEntity), Entities.end());
}