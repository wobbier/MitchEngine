#include "PCH.h"
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

	ComponentData.ComponentTypeList[InComponentTypeId] = true;
	InComponent->Parent = InEntity.GetId();
	//InEntity.SetActive(true);
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
	ComponentData.ComponentTypeList.reset();
}

std::vector<BaseComponent*> ComponentStorage::GetAllComponents(const Entity& InEntity)
{
	std::vector<BaseComponent*> components;
	for (auto& i : ComponentEntries[InEntity.GetId().Index].Components)
	{
		if (i.get())
		{
			components.push_back(i.get());
		}
	}
	return components;
}

void ComponentStorage::Resize(std::size_t InAmount)
{
	ComponentEntries.resize(InAmount);
}

void ComponentStorage::Reset()
{
	ComponentEntries.clear();
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