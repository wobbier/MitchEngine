#pragma once
#include <iostream>
#include "Material.h"
#include "Pointers.h"

typedef SharedPtr<Moonlight::Material> (*CreateMaterialFunc)();

class MaterialInfo
{
public:
	CreateMaterialFunc CreateFunc;
	
#if ME_EDITOR
	std::string Folder;
	std::string Name;
#endif
};

typedef std::map<std::string, MaterialInfo> MaterialRegistry;

inline MaterialRegistry& GetMaterialRegistry()
{
	static MaterialRegistry reg;
	return reg;
}

template<class T>
SharedPtr<Moonlight::Material> CreateMaterial() {
	return MakeShared<T>();
}

template<class T>
struct MaterialRegistryEntry
{
public:
	static MaterialRegistryEntry<T>& Instance(const std::string& name, const std::string& humanName = "", const std::string& folder = "")
	{
		static MaterialRegistryEntry<T> inst(name, humanName, folder);
		return inst;
	}

private:
	MaterialRegistryEntry(const std::string& name, const std::string& humanName, const std::string& folder)
	{
		MaterialRegistry& reg = GetMaterialRegistry();
		CreateMaterialFunc func = CreateMaterial<T>;

		MaterialInfo info;
		info.CreateFunc = func;
#if ME_EDITOR
		info.Folder = folder;
		info.Name = humanName;
#endif
		std::pair<MaterialRegistry::iterator, bool> ret =
			reg.insert(MaterialRegistry::value_type(name, info));

		if (ret.second == false) {
			// Duplicate component register
		}
	}

	MaterialRegistryEntry(const MaterialRegistryEntry<T>&) = delete;
	MaterialRegistryEntry& operator=(const MaterialRegistryEntry<T>&) = delete;
};
