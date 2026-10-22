#include "PCH.h"
#include "BindingContext.h"

#if USING( ME_SCRIPTING )

#include <unordered_map>

namespace ScriptBindings
{
    static WeakPtr<World> gScriptWorld;
    static std::unordered_map<std::string, std::function<bool( EntityHandle )>> gHasComponent;
    static std::unordered_map<std::string, std::function<void( EntityHandle )>> gAddComponent;

    void SetWorld( WeakPtr<World> inWorld )
    {
        gScriptWorld = inWorld;
    }


    WeakPtr<World> GetWorld()
    {
        return gScriptWorld;
    }


    EntityHandle MakeHandle( EntityID inId )
    {
        return EntityHandle( inId, gScriptWorld );
    }


    void RegisterComponentOps( const std::string& inName, std::function<bool( EntityHandle )> inHas, std::function<void( EntityHandle )> inAdd )
    {
        gHasComponent[inName] = std::move( inHas );
        gAddComponent[inName] = std::move( inAdd );
    }


    bool HasComponentNamed( EntityHandle inHandle, const char* inName )
    {
        auto it = gHasComponent.find( inName );
        return it != gHasComponent.end() && it->second( inHandle );
    }


    bool AddComponentNamed( EntityHandle inHandle, const char* inName )
    {
        auto it = gAddComponent.find( inName );
        if( it == gAddComponent.end() )
            return false;
        it->second( inHandle );
        return true;
    }
}

#endif
