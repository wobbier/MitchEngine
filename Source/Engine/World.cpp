#include "PCH.h"
#include "Engine/World.h"
#include "Components/Transform.h"
#include <unordered_map>
#include "Pointers.h"
#include "Logger.h"
#include "ECS/CoreDetail.h"

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

void World::AddCoreByName(const std::string& core)
{
	CoreRegistry& reg = GetCoreRegistry();
	CoreRegistry::iterator it = reg.find(core);

	if (it == reg.end()) {
		Logger::GetInstance().Log(Logger::LogType::Error, "Factory not found for core " + core);
		return;
	}
	std::pair<BaseCore*, TypeId> createdCore = it->second(false);
	if (!HasCore(createdCore.second))
	{
		AddCore(*it->second(true).first, createdCore.second, true);
	}
}

WeakPtr<Entity> World::CreateEntity()
{
	CheckForResize(1);
	EntityCache.Alive.emplace_back(std::make_shared<Entity>(*this, EntIdPool.Create()));
	return EntityCache.Alive.back();
}

void World::Simulate()
{
	if (IsLoading)
	{
		return;
	}
	OPTICK_CATEGORY("World::Simulate", Optick::Category::Scene)
	for (auto& InEntity : EntityCache.Activated)
	{
		auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
		Attr.IsActive = true;

		for (auto& InCore : Cores)
		{
			if (!InCore.second->IsRunning)
			{
				continue;
			}
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
		if (Attr.IsActive)
		{
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
	}

	for (auto& InEntity : EntityCache.Killed)
	{
		DestroyEntity(InEntity);
	}

	EntityCache.ClearTemp();
}

void World::Start()
{
	for (auto& core : Cores)
	{
		if (!core.second->IsRunning)
		{
			core.second->OnStart();
			core.second->IsRunning = true;
		}
	}
}

void World::Stop()
{
	for (auto& core : Cores)
	{
		if (core.second->IsRunning)
		{
			core.second->OnStop();
			core.second->IsRunning = false;
		}
	}
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
		if (InEntity)
		{
			DestroyEntity(*InEntity);
		}
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
	for (auto& core : m_loadedCores)
	{
		core.second->OnStop();
		Cores.erase(core.first);
	}
	m_loadedCores.clear();
}

void World::UpdateLoadedCores(float DeltaTime)
{
	for (auto core : m_loadedCores)
	{
		if (core.second && core.second->IsRunning)
		{
			core.second->Update(DeltaTime);
		}
	}
}

void World::DestroyEntity(Entity &InEntity)
{
	{
		auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
		Attr.IsActive = false;

		for (auto& InCore : Cores)
		{
			auto CoreIndex = InCore.first;

			if (InCore.second->GetComponentFilter().PassFilter(EntityAttributes.Storage.GetComponentTypes(InEntity)))
			{
				if (Attr.Cores.size() >= CoreIndex)
				{
					InCore.second->Remove(InEntity);
					Attr.Cores[CoreIndex] = false;
				}
			}
		}
	}
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

void World::AddCore(BaseCore& InCore, TypeId InCoreTypeId, bool HandleUpdate)
{
	if (!Cores[InCoreTypeId])
	{
		Cores[InCoreTypeId].reset(&InCore);
	}
	InCore.GameWorld = this;
	InCore.Init();
	if (HandleUpdate)
	{
		m_loadedCores[InCoreTypeId] = Cores[InCoreTypeId].get();
	}
}

bool World::HasCore(TypeId InType)
{
	return Cores.find(InType) != Cores.end();
}

BaseCore* World::GetCore(TypeId InType)
{
	return Cores[InType].get();
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

WeakPtr<Entity> World::GetEntity(EntityID id)
{
	for (auto& var : EntityCache.Alive)
	{
		if (var->Id == id)
		{
			return var;
		}
	}
	return {};
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

void World::MarkEntityForDelete(Entity& EntityToDestroy)
{
	EntityCache.Deactivated.push_back(EntityToDestroy);
	EntityCache.Killed.push_back(EntityToDestroy);
}
