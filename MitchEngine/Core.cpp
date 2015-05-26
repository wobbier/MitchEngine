#include "Core.h"
#include "Engine.h"

using namespace ma;

World& BaseCore::GetWorld() const {
	return *GameWorld;
}

std::vector<Entity> BaseCore::GetEntities() const {
	return Entities;
}
