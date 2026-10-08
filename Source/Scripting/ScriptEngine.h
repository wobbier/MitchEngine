#pragma once

#include "Path.h"

#if USING( ME_SCRIPTING )

#include <string>
#include "Pointers.h"
#include "ECS/EntityID.h"

class World;

class ScriptEngine
{
public:
    static int Init();

    // active world
    static void        SetWorld( WeakPtr<World> inWorld );

    static int         CreateScript( const std::string& inName, EntityID inEntity );

    // public-field serialization
    static std::string GetFieldsJson( int inHandle );
    static void        SetFieldsJson( int inHandle, const std::string& inJson );
    static void        ScriptOnStart( int inHandle );
    static void        ScriptOnUpdate( int inHandle, float inDt );
    static void        ScriptOnDestroy( int inHandle );
    static void        ScriptOnEditorInspect( int inHandle );
    static int         GetScriptCount();
    static std::string GetScriptName( int inIndex );
};

#endif