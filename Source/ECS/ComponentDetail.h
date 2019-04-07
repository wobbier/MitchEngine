#pragma once
#include "Component.h"
#include "Entity.h"

typedef BaseComponent* (*CreateComponentFunc)(Entity&);
typedef std::map<std::string, CreateComponentFunc> ComponentRegistry;

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

		std::pair<ComponentRegistry::iterator, bool> ret =
			reg.insert(ComponentRegistry::value_type(name, func));

		if (ret.second == false) {
			// Duplicate component register
		}
	}

	RegistryEntry(const RegistryEntry<T>&) = delete;
	RegistryEntry& operator=(const RegistryEntry<T>&) = delete;
};
