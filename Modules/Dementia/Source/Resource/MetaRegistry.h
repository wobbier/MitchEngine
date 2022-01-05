#pragma once

#pragma once
#include <string>
#include <map>
#include <utility>
#include "ClassTypeId.h"
#include <iostream>
#include "MetaFile.h"
#include "Pointers.h"

typedef SharedPtr<MetaBase>(*CreateMetadataFunc)(const Path& filePath);
typedef std::map<std::string, CreateMetadataFunc> MetaRegistry;

class MetaDatabase
{
public:

	MetaDatabase()
	{

	}

	MetaRegistry reg;
};

inline MetaDatabase& GetMetadatabase()
{
	static MetaDatabase reg;
	return reg;
}

template<class T>
SharedPtr<MetaBase> CreateMetadata(const Path& filePath) {
	return MakeShared<T>(filePath);
}

template<class T>
struct MetaRegistryEntry
{
public:
	static MetaRegistryEntry<T>& Instance(const std::string& ext, const std::string& name)
	{
		static MetaRegistryEntry<T> inst(ext, name);
		return inst;
	}

private:
	MetaRegistryEntry(const std::string& ext, const std::string& name)
	{
		MetaRegistry& reg = GetMetadatabase().reg;
		CreateMetadataFunc func = CreateMetadata<T>;

		std::pair<MetaRegistry::iterator, bool> ret =
			reg.insert(MetaRegistry::value_type(ext, func));

		if (ret.second == false) {
			// Duplicate component register
		}
	}

	MetaRegistryEntry(const MetaRegistryEntry<T>&) = delete;
	MetaRegistryEntry& operator=(const MetaRegistryEntry<T>&) = delete;
};

#define ME_REGISTER_METADATA(EXT, TYPE)                      \
	namespace details {                                  \
    namespace                                            \
    {                                                    \
        template<class T>                                \
        class MetaRegistry;                     \
                                                         \
        template<>                                       \
        class MetaRegistry<TYPE>                \
        {                                                \
            static const MetaRegistryEntry<TYPE>& reg;       \
        };                                               \
                                                         \
        const MetaRegistryEntry<TYPE>&                       \
            MetaRegistry<TYPE>::reg =           \
                MetaRegistryEntry<TYPE>::Instance(EXT, #TYPE);    \
    }}