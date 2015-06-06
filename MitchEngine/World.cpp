#include "World.h"
#include <unordered_map>

using namespace ma;

#define DEFAULT_ENTITY_POOL_SIZE 50

World::World() : World(DEFAULT_ENTITY_POOL_SIZE) {
}

World::World(std::size_t InEntityPoolSize) :
EntIdPool(InEntityPoolSize),
EntityAttributes(InEntityPoolSize) {
}


World::~World() {
}

ma::Entity ma::World::CreateEntity() {
	CheckForResize(1);
	EntityCache.Alive.emplace_back(*this, EntIdPool.Create());
	return EntityCache.Alive.back();
}

void ma::World::Simulate() {
	for (auto& InEntity : EntityCache.Activated) {
		auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
		Attr.IsActive = true;

		for (auto& InCore : Cores) {
			auto CoreIndex = InCore.first;

			if (InCore.second->GetComponentFilter().PassFilter(EntityAttributes.Storage.GetComponentTypes(InEntity))) {
				if (Attr.Cores.size() <= CoreIndex || !Attr.Cores[CoreIndex]) {
					InCore.second->Add(InEntity);

					CheckCapacity(Attr.Cores, CoreIndex);
					Attr.Cores[CoreIndex] = true;
				}
			}
			else if (Attr.Cores.size() > CoreIndex && Attr.Cores[CoreIndex]) {
				InCore.second->Remove(InEntity);
				Attr.Cores[CoreIndex] = false;
			}
		}
	}
	for (auto& InEntity : EntityCache.Deactivated) {
		auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
		Attr.IsActive = false;

		for (auto& InCore : Cores) {
			auto CoreIndex = InCore.first;
			if (Attr.Cores.size() <= CoreIndex) continue;
			if (Attr.Cores[CoreIndex]) {
				InCore.second->Remove(InEntity);
				Attr.Cores[CoreIndex] = false;
			}
		}
	}

	for (auto& InEntity : EntityCache.Killed) {

		EntityCache.Alive.erase(std::remove(EntityCache.Alive.begin(), EntityCache.Alive.end(), InEntity), EntityCache.Alive.end());

		EntityAttributes.Storage.RemoveAllComponents(InEntity);

		EntIdPool.Remove(InEntity.GetId());
	}

	EntityCache.ClearTemp();
}

void ma::World::AddCore(BaseCore& InCore, TypeId InCoreTypeId) {
	Cores[InCoreTypeId].reset(&InCore);
	InCore.GameWorld = this;
	InCore.Init();
}

void ma::World::CheckForResize(std::size_t InNumEntitiesToBeAllocated) {
	auto NewSize = GetEntityCount() + InNumEntitiesToBeAllocated;

	if (NewSize > EntIdPool.GetSize()) {
		Resize(NewSize);
	}
}

void ma::World::Resize(std::size_t InAmount) {
	EntIdPool.Resize(InAmount);
	EntityAttributes.Resize(InAmount);
}

std::size_t ma::World::GetEntityCount() const {
	return EntityCache.Alive.size();
}

void ma::World::ActivateEntity(Entity& InEntity, const bool InActive) {
	if (InActive) {
		EntityCache.Activated.push_back(InEntity);
	}
	else {
		EntityCache.Deactivated.push_back(InEntity);
	}
}
