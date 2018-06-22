#include "ComponentStorage.h"
#include "ComponentTypeArray.h"
#include <iostream>

ComponentStorage::ComponentStorage(std::size_t InEntityAmount) :
	ComponentEntries(InEntityAmount)
{
}

ComponentStorage::~ComponentStorage()
{
}

void ComponentStorage::AddComponent(Entity& InEntity, BaseComponent* InComponent, TypeId InComponentTypeId)
{
	auto Index = InEntity.GetId().Index;

	auto& ComponentData = ComponentEntries[Index];

	CheckCapacity(ComponentData.Components, InComponentTypeId);
	ComponentData.Components[InComponentTypeId].reset(InComponent);

	CheckCapacity(ComponentData.ComponentTypeList, InComponentTypeId);
	ComponentData.ComponentTypeList[InComponentTypeId] = true;
	InComponent->OnCreate(&InEntity);
	InComponent->Init();
}

ComponentTypeArray ComponentStorage::GetComponentTypes(const Entity& InEntity) const
{
	return ComponentEntries[InEntity.GetId().Index].ComponentTypeList;
}

void ComponentStorage::RemoveComponent(const Entity& InEntity, TypeId InTypeId)
{
	auto Index = InEntity.GetId().Index;
	auto& ComponentData = ComponentEntries[Index];

	ComponentData.Components[InTypeId].reset();
	ComponentData.ComponentTypeList[InTypeId] = false;
}

void ComponentStorage::RemoveAllComponents(Entity& InEntity)
{
	auto Index = InEntity.GetId().Index;
	auto& ComponentData = ComponentEntries[Index];

	ComponentData.Components.clear();
	ComponentData.ComponentTypeList.clear();
}

void ComponentStorage::Resize(std::size_t InAmount)
{
	ComponentEntries.resize(InAmount);
}

BaseComponent& ComponentStorage::GetComponent(const Entity& InEntity, TypeId InTypeId)
{
	return *GetComponents_Implementation(InEntity)[InTypeId];
}

ComponentStorage::ImplComponentArray& ComponentStorage::GetComponents_Implementation(const Entity& E)
{
	return ComponentEntries[E.GetId().Index].Components;
}

const ComponentStorage::ImplComponentArray& ComponentStorage::GetComponents_Implementation(const Entity& E) const
{
	return ComponentEntries[E.GetId().Index].Components;
}