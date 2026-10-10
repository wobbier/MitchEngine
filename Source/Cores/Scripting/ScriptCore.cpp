#include "PCH.h"

#include "ScriptCore.h"
#include "Components/Scripting/ScriptComponent.h"
#include "Scripting/ScriptEngine.h"
#include "Cores/NavigationCore.h"
#include "Cores/Physics2DCore.h"
#include "Cores/PhysicsCore.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Physics/Rigidbody2D.h"
#include "Components/Transform.h"
#include "Events/EventManager.h"
#include "Physics/PhysicsTypes.h"
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
    EventManager::GetInstance().RegisterReceiver( this, { CollisionEvent::GetEventId() } );
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
            script.EnsureCreated();
            RefreshSchedule( script );
            pending.push_back( &script );
        }
    }
    if( pending.empty() )
    {
        return;
    }
    // OnStart in [ExecutionOrder] too (equal orders keep their scene order).
    std::stable_sort( pending.begin(), pending.end(), []( const ScriptComponent* InA, const ScriptComponent* InB ) {
        return InA->m_executionOrder < InB->m_executionOrder;
    } );
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
        InScript.m_hadUpdate = false;
        InScript.m_startSequence = m_nextStartSequence++;
        ScriptEngine::ScriptOnStart( InScript.m_dotnetHandle );
    }
#endif
}


void ScriptCore::RefreshSchedule( ScriptComponent& InScript )
{
#if USING( ME_SCRIPTING )
    const uint32_t generation = ScriptEngine::GetReloadGeneration();
    if( InScript.m_scheduleHandle == InScript.m_dotnetHandle && InScript.m_scheduleGeneration == generation )
    {
        return;
    }
    const ScriptEngine::Schedule schedule = ScriptEngine::GetSchedule( InScript.m_dotnetHandle );
    InScript.m_executionOrder = schedule.Order;
    InScript.m_callbacks = schedule.Callbacks;
    InScript.m_scheduleHandle = InScript.m_dotnetHandle;
    InScript.m_scheduleGeneration = generation;
#endif
}


bool ScriptCore::RunsBefore( const ScriptComponent* InA, const ScriptComponent* InB )
{
#if USING( ME_SCRIPTING )
    if( InA->m_executionOrder != InB->m_executionOrder )
    {
        return InA->m_executionOrder < InB->m_executionOrder;
    }
    return InA->m_startSequence < InB->m_startSequence;
#else
    return InA < InB;
#endif
}


const std::vector<ScriptComponent*>& ScriptCore::RunList( uint32_t InCallback )
{
    m_runList.clear();
#if USING( ME_SCRIPTING )
    for( Entity& entity : GetEntities() )
    {
        ScriptComponent& script = entity.GetComponent<ScriptComponent>();
        if( !script.m_started || !script.IsEnabled() || !entity.IsActiveInHierarchy() )
        {
            continue;
        }
        RefreshSchedule( script );
        if( InCallback == ScriptEngine::UpdateCallback )
        {
            script.m_hadUpdate = true;
        }
        else if( InCallback == ScriptEngine::LateUpdateCallback && !script.m_hadUpdate )
        {
            continue;
        }
        if( script.m_callbacks & InCallback )
        {
            m_runList.push_back( &script );
        }
    }
    std::sort( m_runList.begin(), m_runList.end(), &ScriptCore::RunsBefore );
#endif
    return m_runList;
}


void ScriptCore::FixedUpdate( const UpdateContext& inUpdateContext )
{
#if USING( ME_SCRIPTING )
    if( !m_running )
    {
        return;
    }
    OPTICK_EVENT( "ScriptCore::FixedUpdate" );
    for( ScriptComponent* script : RunList( ScriptEngine::FixedUpdateCallback ) )
    {
        ScriptEngine::ScriptOnFixedUpdate( script->m_dotnetHandle, inUpdateContext.GetDeltaTime() );
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
    for( ScriptComponent* script : RunList( ScriptEngine::UpdateCallback ) )
    {
        ScriptEngine::ScriptOnUpdate( script->m_dotnetHandle, inUpdateContext.GetDeltaTime() );
    }
#endif
}


void ScriptCore::LateUpdate( const UpdateContext& inUpdateContext )
{
#if USING( ME_SCRIPTING )
    if( !m_running )
    {
        return;
    }
    OPTICK_EVENT( "ScriptCore::LateUpdate" );
    for( ScriptComponent* script : RunList( ScriptEngine::LateUpdateCallback ) )
    {
        ScriptEngine::ScriptOnLateUpdate( script->m_dotnetHandle, inUpdateContext.GetDeltaTime() );
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


bool ScriptCore::OnEvent( const BaseEvent& InEvent )
{
#if USING( ME_SCRIPTING )
    if( !m_running || InEvent.GetEventId() != CollisionEvent::GetEventId() )
    {
        return false;
    }
    const CollisionEvent& collision = static_cast<const CollisionEvent&>( InEvent );
    const bool enter = collision.State == CollisionEvent::Phase::Enter;
    const ScriptEngine::CollisionCallback kind = collision.IsTrigger
        ? ( enter ? ScriptEngine::CollisionCallback::TriggerEnter : ScriptEngine::CollisionCallback::TriggerExit )
        : ( enter ? ScriptEngine::CollisionCallback::CollisionEnter : ScriptEngine::CollisionCallback::CollisionExit );
    // Each side hears about the other; the normal points from the receiver towards the other.
    DeliverCollision( collision.A, collision.B, static_cast<int>( kind ), collision.Point, collision.Normal );
    DeliverCollision( collision.B, collision.A, static_cast<int>( kind ), collision.Point, collision.Normal * -1.f );
#endif
    return false;
}


void ScriptCore::DeliverCollision( const EntityHandle& InSelf, const EntityHandle& InOther, int InKind, const Vector3& InPoint, const Vector3& InNormal )
{
#if USING( ME_SCRIPTING )
    if( !InSelf )
    {
        return;
    }
    // The collider's entity, and the body it belongs to when that's an ancestor (compound bodies).
    Entity* targets[2] = { InSelf.Get(), nullptr };
    for( Transform* parent = InSelf->GetComponent<Transform>().GetParentTransform(); parent && !InSelf->TryGetComponent<Rigidbody>() && !InSelf->TryGetComponent<Rigidbody2D>(); parent = parent->GetParentTransform() )
    {
        Entity* candidate = parent->Parent.Get();
        if( candidate && ( candidate->TryGetComponent<Rigidbody>() || candidate->TryGetComponent<Rigidbody2D>() ) )
        {
            targets[1] = candidate;
            break;
        }
    }
    const EntityID other = InOther ? InOther.GetID() : EntityID{};
    for( Entity* target : targets )
    {
        ScriptComponent* script = target ? target->TryGetComponent<ScriptComponent>() : nullptr;
        if( script && script->m_started && script->IsEnabled() && script->m_dotnetHandle >= 0 )
        {
            ScriptEngine::ScriptOnCollision( script->m_dotnetHandle, static_cast<ScriptEngine::CollisionCallback>( InKind ), other, InPoint, InNormal );
        }
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
