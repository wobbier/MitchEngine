#pragma once
#include <iostream>

#include "HavanaWidget.h"
#include "Dementia.h"
#include "Pointers.h"

typedef SharedPtr<HavanaWidget> ( *CreateWidgetFunc )();

class WidgetInfo
{
public:
    CreateWidgetFunc CreateFunc;
};

typedef std::map<std::string, WidgetInfo> EditorWidgetRegistry;

inline EditorWidgetRegistry& GetWidgetRegistry()
{
    static EditorWidgetRegistry reg;
    return reg;
}

template<class T>
SharedPtr<HavanaWidget> Create() {
    return MakeShared<T>();
}

template<class T>
struct WidgetRegistryEntry
{
public:
    static WidgetRegistryEntry<T>& Instance( const std::string& name )
    {
        static WidgetRegistryEntry<T> inst( name );
        return inst;
    }

private:
    WidgetRegistryEntry( const std::string& name )
    {
        EditorWidgetRegistry& reg = GetWidgetRegistry();
        CreateWidgetFunc func = Create<T>;

        WidgetInfo info;
        info.CreateFunc = func;
        std::pair<EditorWidgetRegistry::iterator, bool> ret =
            reg.insert( EditorWidgetRegistry::value_type( name, info ) );

        if( ret.second == false ) {
            // Duplicate component register
        }
    }

    WidgetRegistryEntry( const WidgetRegistryEntry<T>& ) = delete;
    WidgetRegistryEntry& operator=( const WidgetRegistryEntry<T>& ) = delete;
};
