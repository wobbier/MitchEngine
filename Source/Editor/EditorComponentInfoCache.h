#pragma once

//#include <Game/World/GameObject/GameObjectDefinitions.h>
#include <unordered_map>
#include "ClassTypeId.h"

#define REGISTER_EDITORCOMPONENTCACHE_ORDERDATA(ComponentType, ComponentOrder)\
{\
	EditorComponentCache::RegisterComponentOrder(ComponentType::GetStaticTypeId(), ComponentOrder);\
}

namespace EditorComponentCache
{
    static constexpr const uint32_t kDefaultSortingOrder = 0x0100;
    struct EditorComponentInfo
    {
        uint32_t Order = kDefaultSortingOrder;
    };
    typedef std::unordered_map<TypeId, EditorComponentInfo> ComponentInfoMap;

    void RegisterComponentOrder( TypeId id, uint32_t componentOrder );

    const ComponentInfoMap& GetAllComponentsInfo();
}