#pragma once
#include <string>
#include <map>
#include <utility>
#include "ClassTypeId.h"
#include "Core/Memory.h"

class BaseCore;

typedef std::pair<BaseCore*, TypeId>( *CreateCoreFunc )( bool );
typedef std::map<std::string, CreateCoreFunc> CoreRegistry;

inline CoreRegistry& GetCoreRegistry()
{
    static CoreRegistry reg;
    return reg;
}

template<class T>
std::pair<BaseCore*, TypeId> CreateCore( bool create ) {
    if( create )
    {
        return std::make_pair( ME_NEW T(), T::GetTypeId() );
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
