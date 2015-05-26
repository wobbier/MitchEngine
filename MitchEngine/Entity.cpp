#include "Entity.h"

using namespace ma;

Entity::Entity() {
}

Entity::Entity(World& inWorld, ID inId) :
Id(inId),
GameWorld(&inWorld) {
}

Entity::~Entity() {
}

template <typename T>
T& Entity::AddComponent(T* inComponent) {
	static_assert(std::is_base_of<BaseComponent, T>(), "T is not a component, cannot add T to entity");
	AddComponent(inComponent, T::GetTypeId());
	return *inComponent;
}

void Entity::AddComponent(BaseComponent* inComponent, Type inComponentTypeId) {
	//GameWorld->m_entityAttributes.componentStorage.addComponent(*this, inComponent, inComponentTypeId);
}
