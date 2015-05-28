#include "ComponentStorage.h"
#include "ComponentTypeArray.h"
#include <iostream>

using namespace ma;

ComponentStorage::ComponentStorage(std::size_t InEntityAmount) :
ComponentEntries(InEntityAmount) {
}


ComponentStorage::~ComponentStorage() {
}

void ma::ComponentStorage::AddComponent(Entity& InEntity, BaseComponent* InComponent, TypeId InComponentTypeId) {
	auto Index = InEntity.GetId().Index;

	auto& ComponentData = ComponentEntries[Index];

	CheckCapacity(ComponentData.Components, InComponentTypeId);
	ComponentData.Components[InComponentTypeId].reset(InComponent);

	CheckCapacity(ComponentData.ComponentTypeList, InComponentTypeId);
	ComponentData.ComponentTypeList[InComponentTypeId] = true;
}

ma::ComponentTypeArray ma::ComponentStorage::GetComponentTypes(const Entity& InEntity) const {
	return ComponentEntries[InEntity.GetId().Index].ComponentTypeList;
}

void ComponentStorage::Resize(std::size_t InAmount) {
	std::cout << "RESIZE NOT IMPLEMENTED!";
}

BaseComponent& ComponentStorage::GetComponent(const Entity& InEntity, TypeId InTypeId) {
	return *GetComponents_Implementation(InEntity)[InTypeId];
}

ComponentStorage::ImplComponentArray& ComponentStorage::GetComponents_Implementation(const Entity& E) {
	return ComponentEntries[E.GetId().Index].Components;
}

const ComponentStorage::ImplComponentArray& ComponentStorage::GetComponents_Implementation(const Entity& E) const {
	return ComponentEntries[E.GetId().Index].Components;
}
