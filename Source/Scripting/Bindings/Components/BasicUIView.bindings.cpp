#include "PCH.h"
#include "BasicUIView.bindings.h"

#if USING( ME_SCRIPTING )

#include <Components/UI/BasicUIView.h>
#include "ECS/Entity.h"
#include "Scripting/Bindings/BindingContext.h"

using ScriptBindings::MakeHandle;

static void Eng_BasicUIView_ExecuteJS( EntityID inId, const uint8_t* inJS )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        handle->GetComponent<BasicUIView>().ExecuteScript( reinterpret_cast<const char*>( inJS ) );
    }
}


void Register_BasicUIViewBindings( ScriptEngineAPI& inAPI )
{
    ScriptBindings::RegisterComponent<BasicUIView>( "BasicUIView" );

    inAPI.BasicUIView_ExecuteJS = Eng_BasicUIView_ExecuteJS;
}

#endif
