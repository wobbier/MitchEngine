#include "ComponentStorage.h"
#include <iostream>

using namespace ma;

ComponentStorage::ComponentStorage(std::size_t InEntityAmount) :
ComponentEntries(InEntityAmount) {
}


ComponentStorage::~ComponentStorage() {
}

void ma::ComponentStorage::AddComponent(Entity& InEntity, BaseComponent* InComponent, Type InComponentTypeId) {
	auto Index = InEntity.GetId().Index;

	auto& ComponentData = ComponentEntries[Index];

	ComponentData.Components[InComponentTypeId].reset(InComponent);
	ComponentData.ComponentTypeList[InComponentTypeId] = true;
}

void ma::ComponentStorage::Resize(std::size_t InAmount) {
	std::cout << "RESIZE NOT IMPLEMENTED!";
}
