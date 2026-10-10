#pragma once
#include <string>
#include <map>
#include <utility>
#include "ClassTypeId.h"
#include <iostream>

class BaseCore;

typedef std::pair<BaseCore*, TypeId>( *CreateCoreFunc )( bool );
typedef std::map<std::string, CreateCoreFunc> CoreRegistry;

inline CoreRegistry& GetCoreRegistry()
{
    static CoreRegistry reg;
    return reg;
}

// Former core names (types renamed since scenes were saved) -> current names.
inline std::map<std::string, std::string>& GetCoreAliases()
{
    static std::map<std::string, std::string> aliases;
    return aliases;
}

// The registry entry for a core name, following renames. end() when unknown.
inline CoreRegistry::iterator FindCoreFactory( const std::string& InName )
{
    CoreRegistry& reg = GetCoreRegistry();
    CoreRegistry::iterator it = reg.find( InName );
    if( it == reg.end() )
    {
        auto alias = GetCoreAliases().find( InName );
        if( alias != GetCoreAliases().end() )
        {
            it = reg.find( alias->second );
        }
    }
    return it;
}

struct CoreAlias
{
    CoreAlias( const char* InFormerName, const char* InName )
    {
        GetCoreAliases()[InFormerName] = InName;
    }
};

template<class T>
std::pair<BaseCore*, TypeId> CreateCore( bool create ) {
    if( create )
    {
        return std::make_pair( new T(), T::GetTypeId() );
    }
    return std::make_pair( nullptr, T::GetTypeId() );
}

template<class T>
struct CoreRegistryEntry
{
public:
    static CoreRegistryEntry<T>& Instance( const std::string& name )
    {
        static CoreRegistryEntry<T> inst( name );
        return inst;
    }

private:
    CoreRegistryEntry( const std::string& name )
    {
        CoreRegistry& reg = GetCoreRegistry();
        CreateCoreFunc func = CreateCore<T>;

        std::pair<CoreRegistry::iterator, bool> ret =
            reg.insert( CoreRegistry::value_type( name, func ) );

        if( ret.second == false ) {
            // Duplicate component register
        }
    }

    CoreRegistryEntry( const CoreRegistryEntry<T>& ) = delete;
    CoreRegistryEntry& operator=( const CoreRegistryEntry<T>& ) = delete;
};
