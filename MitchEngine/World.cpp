#include "World.h"

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
		}
	}
}

void ma::World::AddCore(BaseCore& InCore, TypeId InCoreTypeId) {

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
