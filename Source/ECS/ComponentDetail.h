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
	
#if USING( ME_EDITOR )
	std::string Folder;
#endif
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
	static RegistryEntry<T>& Instance(const std::string& name, const std::string& folder = "")
	{
		static RegistryEntry<T> inst(name, folder);
		return inst;
	}

private:
	RegistryEntry(const std::string& name, const std::string& folder)
	{
		ComponentRegistry& reg = GetComponentRegistry();
		CreateComponentFunc func = AddComponent<T>;
		GetComponentType typeFunc = GetComponentTypeImpl<T>;

		ComponentInfo info;
		info.CreateFunc = func;
		info.GetTypeFunc = typeFunc;
#if USING( ME_EDITOR )
		info.Folder = folder;
#endif
		std::pair<ComponentRegistry::iterator, bool> ret =
			reg.insert(ComponentRegistry::value_type(name, info));

		if (ret.second == false) {
			// Duplicate component register
		}
	}

	RegistryEntry(const RegistryEntry<T>&) = delete;
	RegistryEntry& operator=(const RegistryEntry<T>&) = delete;
};
