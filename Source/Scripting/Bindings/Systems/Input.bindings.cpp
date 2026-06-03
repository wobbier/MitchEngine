#include "PCH.h"
#include "Input.bindings.h"

#if USING( ME_SCRIPTING )

#include "Engine/Engine.h"
#include "Engine/Input.h"


static bool Eng_Input_IsKeyDown( int inKey )
{
    return GetEngine().GetInput().IsKeyDown( static_cast<KeyCode>( inKey ) );
}


void Register_InputBindings( ScriptEngineAPI& inAPI )
{
    inAPI.Input_IsKeyDown = Eng_Input_IsKeyDown;
}


#endif
