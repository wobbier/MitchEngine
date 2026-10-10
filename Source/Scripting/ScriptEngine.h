#pragma once

#include "Path.h"

#if USING( ME_SCRIPTING )

#include <string>
#include "Pointers.h"
#include "ECS/EntityID.h"
#include "Math/Vector3.h"

class World;

class ScriptEngine
{
public:
    // The command-line project of the game's scripts (references ScriptCore.csproj).
    static constexpr const char* kScriptProject = "Project/Game.Script.csproj";

    // Starts .NET and loads ScriptCore + Game.Script from the executable's directory; tools builds
    // first rebuild them when their sources changed. Returns 0 on success.
    static int Init();
    static bool IsAvailable();
    static void Shutdown();

    // dotnet build of kScriptProject into the executable's directory (tools builds).
    static bool NeedsBuild();
    static bool BuildScripts( std::string* OutLog = nullptr );
    // Hot reload: RequestReload rebuilds on a worker thread; PollReload (every frame, main thread)
    // swaps the assembly in when the build finishes. Live scripts keep their handles and fields.
    static void RequestReload();
    static bool PollReload();
    static bool Reload();
    static bool IsHandleAlive( int inHandle );

    // active world
    static void        SetWorld( WeakPtr<World> inWorld );

    static int         CreateScript( const std::string& inName, EntityID inEntity );

    // public-field serialization
    static std::string GetFieldsJson( int inHandle );
    static void        SetFieldsJson( int inHandle, const std::string& inJson );
    static void        ScriptOnStart( int inHandle );
    static void        ScriptOnUpdate( int inHandle, float inDt );
    static void        ScriptOnFixedUpdate( int inHandle, float inDt );
    // Physics callbacks (OnCollisionEnter / Exit, OnTriggerEnter / Exit in C#).
    enum class CollisionCallback : int
    {
        CollisionEnter = 0,
        CollisionExit,
        TriggerEnter,
        TriggerExit,
    };
    static void        ScriptOnCollision( int inHandle, CollisionCallback inKind, EntityID inOther, const Vector3& inPoint, const Vector3& inNormal );
    static void        ScriptOnDestroy( int inHandle );
    static void        ScriptOnEditorInspect( int inHandle );
    static int         GetScriptCount();
    static std::string GetScriptName( int inIndex );
};

#endif