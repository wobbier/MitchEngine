#include "PCH.h"
#include "Engine/World.h"
#include "Components/Transform.h"
#include <unordered_map>

#define DEFAULT_ENTITY_POOL_SIZE 50

World::World()
	: World(DEFAULT_ENTITY_POOL_SIZE)
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

WeakPtr<Entity> World::CreateEntity()
{
	CheckForResize(1);
	EntityCache.Alive.emplace_back(std::make_shared<Entity>(*this, EntIdPool.Create()));
	return EntityCache.Alive.back();
}

void World::Simulate()
{
	BROFILER_CATEGORY("World::Simulate", Brofiler::Color::Blue)
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

		EntityAttributes.Storage.RemoveAllComponents(InEntity);

		EntIdPool.Remove(InEntity.GetId());


		for (auto i = EntityCache.Alive.begin(); i != EntityCache.Alive.end(); ++i)
		{
			if (*(*i).get() == InEntity)
			{
				EntityCache.Alive.erase(i);
				break;
			}
		}
	}

	EntityCache.ClearTemp();
}

void World::Destroy()
{

	for (auto& InEntity : EntityCache.Killed)
	{
		DestroyEntity(InEntity);
	}

	for (auto& InEntity : EntityCache.Deactivated)
	{
		DestroyEntity(InEntity);
	}

	for (auto& InEntity : EntityCache.Activated)
	{
		DestroyEntity(InEntity);
	}

	for (auto& InEntity : EntityCache.Alive)
	{
		EntityAttributes.Storage.RemoveAllComponents(*InEntity);
		auto& Attr = EntityAttributes.Attributes[(*InEntity).GetId().Index];
		Attr.Cores.reset();

		EntIdPool.Remove(InEntity->GetId());
	}

	EntityCache.Alive.clear();
	EntityCache.Deactivated.clear();
	EntityCache.Killed.clear();

	EntityCache.ClearTemp();
}

void World::Cleanup()
{
	Destroy();
	EntIdPool.Reset();
	EntityAttributes.Storage.Reset();
	for (auto& core : Cores)
	{
		//auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
		core.second->Clear();
	}
}

void World::DestroyEntity(Entity &InEntity)
{
	EntityAttributes.Storage.RemoveAllComponents(InEntity);
	auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
	Attr.Cores.reset();

	EntIdPool.Remove(InEntity.GetId());

	for (auto i = EntityCache.Alive.begin(); i != EntityCache.Alive.end(); ++i)
	{
		if (*(*i).get() == InEntity)
		{
			EntityCache.Alive.erase(i);
			break;
		}
	}
}

void World::AddCore(BaseCore& InCore, TypeId InCoreTypeId)
{
	if (!Cores[InCoreTypeId])
	{
		Cores[InCoreTypeId].reset(&InCore);
	}
	InCore.GameWorld = this;
	InCore.Init();
}

std::vector<BaseCore*> World::GetAllCores()
{
	std::vector<BaseCore*> cores;
	for (auto& core : Cores)
	{
		cores.push_back(core.second.get());
	}
	return cores;
}

void World::CheckForResize(std::size_t InNumEntitiesToBeAllocated)
{
	auto NewSize = GetEntityCount() + InNumEntitiesToBeAllocated;

	if (NewSize > EntIdPool.GetSize())
	{
		Resize(NewSize);
	}
}

void World::Resize(std::size_t InAmount)
{
	EntIdPool.Resize(InAmount);
	EntityAttributes.Resize(InAmount);
}

std::size_t World::GetEntityCount() const
{
	return EntityCache.Alive.size();
}

Entity* World::GetEntity(EntityID id)
{
	for (auto& var : EntityCache.Alive)
	{
		if (var->Id == id)
		{
			return var.get();
		}
	}
	return nullptr;
}

void World::ActivateEntity(Entity& InEntity, const bool InActive)
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