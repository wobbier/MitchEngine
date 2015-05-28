#include "Entity.h"
#include "World.h"
using namespace ma;

Entity::Entity() {
}

Entity::Entity(World& inWorld, ID inId) :
Id(inId),
GameWorld(&inWorld) {
}

Entity::~Entity() {

}

void Entity::AddComponent(BaseComponent* inComponent, TypeId inComponentTypeId) {
	GameWorld->EntityAttributes.Storage.AddComponent(*this, inComponent, inComponentTypeId);
}

const Entity::ID& Entity::GetId() const {
	return Id;
}

BaseComponent& Entity::GetComponent(TypeId InTypeId) const {
	return GameWorld->EntityAttributes.Storage.GetComponent(*this, InTypeId);
}
