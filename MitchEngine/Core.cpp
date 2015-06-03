#include "Core.h"

using namespace ma;

BaseCore::BaseCore(const ComponentFilter& Filter) : CompFilter(Filter){

}

World& BaseCore::GetWorld() const {
	return *GameWorld;
}

std::vector<Entity> BaseCore::GetEntities() const {
	return Entities;
}

const ComponentFilter& BaseCore::GetComponentFilter() const {
	return CompFilter;
}

void BaseCore::Add(Entity& InEntity) {
	Entities.push_back(InEntity);
}

ma::BaseCore::~BaseCore() {

}

void ma::BaseCore::Remove(Entity& InEntity) {
	Entities.erase(std::remove(Entities.begin(), Entities.end(), InEntity), Entities.end());
}

