#include "Entity.h"

using namespace ma;

Entity::Entity() {
}

Entity::~Entity() {
}

Component* Entity::GetComponent(int id) {
	return Components.at(id);
}

void Entity::AddComponent(Component* component) {
	Components.push_back(component);
	component->Handle = Components.size() - 1;
	component->Entity = this;
}

bool Entity::HasComponent(int id) {

}