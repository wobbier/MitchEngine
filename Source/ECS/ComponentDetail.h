#pragma once
#include "Component.h"
#include "Entity.h"
#include <iostream>

typedef BaseComponent* (*CreateComponentFunc)(Entity&);
typedef TypeId (*GetComponentType)();

class ComponentInfo
{
public:
	CreateComponentFunc CreateFunc;
	GetComponentType GetTypeFunc;
};

typedef std::map<std::string, ComponentInfo> ComponentRegistry;

inline ComponentRegistry& GetComponentRegistry()
{
	static ComponentRegistry reg;
	return reg;
}

template<class T>
BaseComponent* AddComponent(Entity& inEnt) {
	return &inEnt.AddComponent<T>();
}

template<class T>
TypeId GetComponentTypeImpl() {
	return Component<T>::GetTypeId();
}

template<class T>
struct RegistryEntry
{
public:
	static RegistryEntry<T>& Instance(const std::string& name)
	{
		static RegistryEntry<T> inst(name);
		return inst;
	}

private:
	RegistryEntry(const std::string& name)
	{
		ComponentRegistry& reg = GetComponentRegistry();
		CreateComponentFunc func = AddComponent<T>;
		GetComponentType typeFunc = GetComponentTypeImpl<T>;

		ComponentInfo info;
		info.CreateFunc = func;
		info.GetTypeFunc = typeFunc;

		std::pair<ComponentRegistry::iterator, bool> ret =
			reg.insert(ComponentRegistry::value_type(name, info));

		if (ret.second == false) {
			// Duplicate component register
		}
	}

	RegistryEntry(const RegistryEntry<T>&) = delete;
	RegistryEntry& operator=(const RegistryEntry<T>&) = delete;
};
