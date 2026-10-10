#include "PCH.h"

#include "ScriptCore.h"
#include "Components/Scripting/ScriptComponent.h"
#include "Scripting/ScriptEngine.h"
#include "Cores/NavigationCore.h"
#include "Cores/Physics2DCore.h"
#include "Cores/PhysicsCore.h"
#include "Engine/Engine.h"
#include "optick.h"

#if USING( ME_EDITOR )
#include "imgui.h"
#endif


ScriptCore::ScriptCore()
    : Base( ComponentFilter().Requires<ScriptComponent>() )
{
    SetIsSerializable( false );

#if USING( ME_SCRIPTING )
    ScriptEngine::Init();
#endif
}


void ScriptCore::OnStart()
{
    m_running = true;
    StartPendingScripts();
}


void ScriptCore::StartPendingScripts()
{
#if USING( ME_SCRIPTING )
    std::vector<ScriptComponent*> pending;
    for( Entity& entity : GetEntities() )
    {
        ScriptComponent& script = entity.GetComponent<ScriptComponent>();
        if( !script.m_started && entity.IsActiveInHierarchy() )
        {
            pending.push_back( &script );
        }
    }
    if( pending.empty() )
    {
        return;
    }
    // OnStart sees every body and navmesh that exists by now, including ones spawned this frame.
    if( NavigationCore* navigation = GetEngine().Navigation )
    {
        navigation->SyncNow();
    }
    if( PhysicsCore* physics = GetEngine().Physics )
    {
        physics->SyncNow();
    }
    if( Physics2DCore* physics2D = GetEngine().Physics2D )
    {
        physics2D->SyncNow();
    }
    for( ScriptComponent* script : pending )
    {
        StartScript( *script );
    }
#endif
}


void ScriptCore::OnStop()
{
    m_running = false;
}


void ScriptCore::StartScript( ScriptComponent& InScript )
{
#if USING( ME_SCRIPTING )
    InScript.EnsureCreated();
    if( InScript.m_dotnetHandle >= 0 && !InScript.m_started )
    {
        InScript.m_started = true;
        ScriptEngine::ScriptOnStart( InScript.m_dotnetHandle );
    }
#endif
}


void ScriptCore::FixedUpdate( const UpdateContext& inUpdateContext )
{
#if USING( ME_SCRIPTING )
    if( !m_running )
    {
        return;
    }
    OPTICK_EVENT( "ScriptCore::FixedUpdate" );
    for( Entity& entity : GetEntities() )
    {
        ScriptComponent& script = entity.GetComponent<ScriptComponent>();
        if( script.m_started && script.IsEnabled() && entity.IsActiveInHierarchy() )
        {
            ScriptEngine::ScriptOnFixedUpdate( script.m_dotnetHandle, inUpdateContext.GetDeltaTime() );
        }
    }
#endif
}


void ScriptCore::Update( const UpdateContext& inUpdateContext )
{
#if USING( ME_SCRIPTING )
    if( !m_running )
    {
        return;
    }
    OPTICK_EVENT( "ScriptCore::Update" );
    // Scripts created since the world started (spawned, or loaded with a scene) start before
    // their first update.
    StartPendingScripts();
    for( Entity& entity : GetEntities() )
    {
        ScriptComponent& script = entity.GetComponent<ScriptComponent>();
        if( script.m_started && script.IsEnabled() && entity.IsActiveInHierarchy() )
        {
            ScriptEngine::ScriptOnUpdate( script.m_dotnetHandle, inUpdateContext.GetDeltaTime() );
        }
    }
#endif
}


void ScriptCore::OnEntityAdded( Entity& NewEntity )
{
    // ScriptComponent::Init creates the instance; spawned during play, it starts before the next
    // update (StartPendingScripts), once physics has its bodies.
}


void ScriptCore::OnEntityRemoved( Entity& InEntity )
{
#if USING( ME_SCRIPTING )
    if( ScriptComponent* script = InEntity.TryGetComponent<ScriptComponent>() )
    {
        script->DestroyInstance();
    }
#endif
}


#if USING( ME_EDITOR )
void ScriptCore::OnEditorInspect()
{
#if USING( ME_SCRIPTING )
    OPTICK_EVENT( "ScriptCore::OnEditorInspect" );
    ImGui::Text( "Scripting: %s", ScriptEngine::IsAvailable() ? "running" : "unavailable" );
    if( ImGui::Button( "Rebuild && Reload Scripts" ) )
    {
        ScriptEngine::RequestReload();
    }
    if( ImGui::CollapsingHeader( "Script Classes" ) )
    {
        const int count = ScriptEngine::GetScriptCount();
        for( int i = 0; i < count; ++i )
        {
            ImGui::BulletText( "%s", ScriptEngine::GetScriptName( i ).c_str() );
        }
    }
#endif
}
#endif
