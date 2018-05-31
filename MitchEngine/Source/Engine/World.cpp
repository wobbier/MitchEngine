#include "Engine/World.h"
#include "Components/Transform.h"
#include <unordered_map>

using namespace MAN;

#define DEFAULT_ENTITY_POOL_SIZE 50

World::World() : World(DEFAULT_ENTITY_POOL_SIZE)
{
}

World::World(std::size_t InEntityPoolSize) :
	EntIdPool(InEntityPoolSize),
	EntityAttributes(InEntityPoolSize)
{
}

World::~World()
{
}

MAN::Entity MAN::World::CreateEntity()
{
	CheckForResize(1);
	EntityCache.Alive.emplace_back(*this, EntIdPool.Create());
	EntityCache.Alive.back().AddComponent<Transform>();
	EntityCache.Alive.back().SetActive(true);
	return EntityCache.Alive.back();
}

void MAN::World::Simulate()
{
	for (auto& InEntity : EntityCache.Activated)
	{
		auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
		Attr.IsActive = true;

		for (auto& InCore : Cores)
		{
			auto CoreIndex = InCore.first;

			if (InCore.second->GetComponentFilter().PassFilter(EntityAttributes.Storage.GetComponentTypes(InEntity)))
			{
				if (Attr.Cores.size() <= CoreIndex || !Attr.Cores[CoreIndex])
				{
					InCore.second->Add(InEntity);

					CheckCapacity(Attr.Cores, CoreIndex);
					Attr.Cores[CoreIndex] = true;
				}
			}
			else if (Attr.Cores.size() > CoreIndex && Attr.Cores[CoreIndex])
			{
				InCore.second->Remove(InEntity);
				Attr.Cores[CoreIndex] = false;
			}
		}
	}
	for (auto& InEntity : EntityCache.Deactivated)
	{
		auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
		Attr.IsActive = false;

		for (auto& InCore : Cores)
		{
			auto CoreIndex = InCore.first;
			if (Attr.Cores.size() <= CoreIndex) continue;
			if (Attr.Cores[CoreIndex])
			{
				InCore.second->Remove(InEntity);
				Attr.Cores[CoreIndex] = false;
			}
		}
	}

	for (auto& InEntity : EntityCache.Killed)
	{
		EntityCache.Alive.erase(std::remove(EntityCache.Alive.begin(), EntityCache.Alive.end(), InEntity), EntityCache.Alive.end());

		EntityAttributes.Storage.RemoveAllComponents(InEntity);

		EntIdPool.Remove(InEntity.GetId());
	}

	EntityCache.ClearTemp();
}

void MAN::World::AddCore(BaseCore& InCore, TypeId InCoreTypeId)
{
	Cores[InCoreTypeId].reset(&InCore);
	InCore.GameWorld = this;
	InCore.Init();
}

void MAN::World::CheckForResize(std::size_t InNumEntitiesToBeAllocated)
{
	auto NewSize = GetEntityCount() + InNumEntitiesToBeAllocated;

	if (NewSize > EntIdPool.GetSize())
	{
		Resize(NewSize);
	}
}

void MAN::World::Resize(std::size_t InAmount)
{
	EntIdPool.Resize(InAmount);
	EntityAttributes.Resize(InAmount);
}

std::size_t MAN::World::GetEntityCount() const
{
	return EntityCache.Alive.size();
}

void MAN::World::ActivateEntity(Entity& InEntity, const bool InActive)
{
	if (InActive)
	{
		EntityCache.Activated.push_back(InEntity);
	}
	else
	{
		EntityCache.Deactivated.push_back(InEntity);
	}
}