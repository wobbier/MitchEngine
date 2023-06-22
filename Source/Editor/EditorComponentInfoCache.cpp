#include "PCH.h"
#include "EditorComponentInfoCache.h"

namespace EditorComponentCache
{
    ComponentInfoMap s_componentTypeRegistry;

    void RegisterComponentOrder( TypeId id, uint32_t componentOrder )
    {
        s_componentTypeRegistry[id].Order = componentOrder;
    }

    const ComponentInfoMap& GetAllComponentsInfo()
    {
        return s_componentTypeRegistry;
    }
}