#pragma once

#include "Path.h"

#if USING( ME_SCRIPTING )

#include <string>
#include <vector>
#include "Pointers.h"
#include "ECS/EntityID.h"
#include "Math/Vector3.h"

class World;

class ScriptEngine
{
public:
    // The command-line project of the game's scripts (references ScriptCore.csproj).
    static constexpr const char* kScriptProject = "Project/Game.Script.csproj";

    // Starts .NET and loads ScriptCore + Game.Script from GetAssemblyDirectory(); tools builds
    // first rebuild them when their sources changed. Returns 0 on success. The start runs once:
    // later calls return its result, waiting for a StartInitAsync still under way.
    static int Init();
    // Where the script assemblies live, with a trailing slash: next to the executable (shipped
    // builds, and a project's own builds in <project>/.build/<config>/), or for a shared editor or
    // Player running a project with a script project, <project>/.tmp/Scripts/<Executable>-<Debug|Release>/.
    static std::string GetAssemblyDirectory();
    // Runs the start on a worker thread, so .NET comes up while the window and renderer are created.
    static void StartInitAsync();
    static bool IsAvailable();
    // The last script build failed: its errors are in the log, and the editor won't Play until they're fixed.
    static bool HasBuildErrors();

    // One compiler diagnostic from dotnet build's output, its path relative to the project root.
    struct BuildDiagnostic
    {
        std::string File;
        int Line = 0;
        int Column = 0;
        bool IsError = false;
        std::string Code;
        std::string Message;
    };
    // Every diagnostic in a build's output, once each, with paths under InRoot made relative to it.
    static std::vector<BuildDiagnostic> ParseBuildDiagnostics( const std::string& InOutput, const std::string& InRoot );
    static void Shutdown();

    // dotnet build of kScriptProject into GetAssemblyDirectory() (tools builds).
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
    static void        ScriptOnLateUpdate( int inHandle, float inDt );
    // How a live script is scheduled: its [ExecutionOrder] and which update callbacks its class
    // implements (ScheduleCallback bits), so the engine only calls those.
    enum ScheduleCallback : uint32_t
    {
        FixedUpdateCallback = 1,
        UpdateCallback = 2,
        LateUpdateCallback = 4,
    };
    struct Schedule
    {
        int Order = 0;
        uint32_t Callbacks = 0;     // none for a dead handle
    };
    static Schedule    GetSchedule( int inHandle );
    // Bumped by each hot reload: script classes (so their schedules) may have changed.
    static uint32_t    GetReloadGeneration();
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
    // The class of a live script: differs from the name it was created with when the class was
    // renamed with [FormerName]. Empty for dead handles.
    static std::string GetHandleTypeName( int inHandle );
};

#endif