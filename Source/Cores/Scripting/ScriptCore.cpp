#include "PCH.h"

#include "ScriptCore.h"
#include "Components/Scripting/ScriptComponent.h"
#include "Scripting/ScriptEngine.h"
#include "Cores/NavigationCore.h"
#include "Cores/NetCore.h"
#include "Cores/Physics2DCore.h"
#include "Cores/PhysicsCore.h"
#include "Components/Transform.h"
#include "Events/EventManager.h"
#include "Physics/CollisionRouter.h"
#include "Physics/PhysicsTypes.h"
#include "Engine/Engine.h"
#include "Components/Animation/Animator.h"
#include "Components/Animation/Sequence.h"
#include "Events/PlatformEvents.h"
#include "Events/SceneEvents.h"
#include "Input/Gamepads.h"
#include "Input/InputPlayers.h"
#include "World/Scene.h"
#include "Events/EditorEvents.h"
#include "Path.h"
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
    Listen<CollisionEvent>( [this]( const CollisionEvent& InEvent ) { OnCollision( InEvent ); } );
    // Engine events for C# (EventBus): queued, then published once before the next update.
    const auto entityJson = []( const EntityID& InId ) { return json::array( { InId.Index, InId.Generation } ); };
    Listen<SceneLoadedEvent>( [this]( const SceneLoadedEvent& InEvent ) {
        QueueEngineEvent( { { "Type", "SceneLoaded" }, { "Path", InEvent.LoadedScene ? InEvent.LoadedScene->FilePath.GetLocalPathString() : std::string() } } );
    } );
    Listen<AdditiveSceneEvent>( [this]( const AdditiveSceneEvent& InEvent ) {
        QueueEngineEvent( { { "Type", "AdditiveScene" }, { "SceneId", InEvent.SceneId }, { "Path", InEvent.Path }, { "Loaded", InEvent.Loaded } } );
    } );
    Listen<GamepadConnectionEvent>( [this]( const GamepadConnectionEvent& InEvent ) {
        QueueEngineEvent( { { "Type", "Gamepad" }, { "Slot", InEvent.Slot }, { "Connected", InEvent.Connected }, { "Name", InEvent.Name } } );
    } );
    Listen<InputPlayerEvent>( [this]( const InputPlayerEvent& InEvent ) {
        QueueEngineEvent( { { "Type", "Player" }, { "Player", InEvent.Player }, { "GamepadSlot", InEvent.GamepadSlot }, { "Change", static_cast<int>( InEvent.Type ) } } );
    } );
    Listen<AnimationEvent>( [this, entityJson]( const AnimationEvent& InEvent ) {
        QueueEngineEvent( { { "Type", "Animation" }, { "Entity", entityJson( InEvent.Entity.GetID() ) }, { "State", InEvent.State }, { "Name", InEvent.Name } } );
    } );
    // Sequencer markers and playback notifications (GAME-4); an unbound receiver is [0, 0].
    Listen<SequenceEvent>( [this, entityJson]( const SequenceEvent& InEvent ) {
        if( !m_running )
        {
            return;
        }
        QueueEngineEvent( { { "Type", "Sequence" }, { "Player", entityJson( InEvent.Player.GetID() ) }, { "Receiver", entityJson( InEvent.Receiver.GetID() ) },
            { "Kind", static_cast<int>( InEvent.Kind ) }, { "Name", InEvent.Name }, { "Payload", InEvent.Payload }, { "Number", InEvent.Number },
            { "Frame", InEvent.Frame }, { "Clip", InEvent.Clip } } );
    } );
    // Project files edited while playing (tools builds): data assets re-read in place.
    Listen<AssetsChangedEvent>( [this]( const AssetsChangedEvent& InEvent ) {
        if( !m_running )
        {
            return;
        }
        json paths = json::array();
        for( const std::string& path : InEvent.Paths )
        {
            paths.push_back( Path( path ).GetLocalPathString() );
        }
        QueueEngineEvent( { { "Type", "AssetsChanged" }, { "Paths", paths } } );
    } );
    Listen<WindowResizedEvent>( [this]( const WindowResizedEvent& InEvent ) {
        QueueEngineEvent( { { "Type", "WindowResized" }, { "Size", { InEvent.NewSize.x, InEvent.NewSize.y } } } );
    } );
#endif
}


void ScriptCore::OnStart()
{
    m_running = true;
#if USING( ME_SCRIPTING )
    // A fresh scheduler, with the main-thread context in place before any OnStart awaits.
    ScriptEngine::SchedulerTick( 0.f, true );
#endif
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
    // This world's cores (NET-0: a world simulated alongside the game's has its own).
    if( NavigationCore* navigation = GetWorld().GetCore<NavigationCore>() )
    {
        navigation->SyncNow();
    }
    if( PhysicsCore* physics = GetWorld().GetCore<PhysicsCore>() )
    {
        physics->SyncNow();
    }
    if( Physics2DCore* physics2D = GetWorld().GetCore<Physics2DCore>() )
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
    // Play starts fresh: nothing from the last session (or edit mode) is delivered late, and no
    // await, coroutine, timer or tween outlives it.
    m_engineEvents.clear();
#if USING( ME_SCRIPTING )
    ScriptEngine::SchedulerTick( 0.f, true );
#endif
}


void ScriptCore::QueueEngineEvent( json InEvent )
{
    // Bounded: events pile up while nothing runs (edit mode, a paused game).
    constexpr size_t kMaxQueued = 256;
    if( m_engineEvents.size() < kMaxQueued )
    {
        m_engineEvents.push_back( std::move( InEvent ) );
    }
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
    // In a network session a client simulates only its predicted pawn's networked scripts; the
    // server's state drives the rest (NET-6).
    NetCore* net = GetWorld().GetCore<NetCore>();
    const bool filter = net && net->IsClient();
    for( ScriptComponent* script : RunList( ScriptEngine::FixedUpdateCallback ) )
    {
        if( filter && script->Parent && !net->SimulatesLocally( *script->Parent.Get() ) )
        {
            continue;
        }
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
    if( !m_engineEvents.empty() )
    {
        // Moved out first: a handler that loads a scene queues new events for the next frame.
        const json events = json( std::exchange( m_engineEvents, {} ) );
        ScriptEngine::DispatchEngineEvents( events.dump() );
    }
    // Awaits, coroutines, timers and tweens advance by game time before OnUpdate.
    ScriptEngine::SchedulerTick( inUpdateContext.GetDeltaTime() );
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


void ScriptCore::OnCollision( const CollisionEvent& collision )
{
#if USING( ME_SCRIPTING )
    // Another world's physics (a server or rollback simulation) isn't this world's business.
    if( !m_running || ( collision.Source != nullptr && collision.Source != &GetWorld() ) )
    {
        return;
    }
    const bool enter = collision.State == CollisionEvent::Phase::Enter;
    const ScriptEngine::CollisionCallback kind = collision.IsTrigger
        ? ( enter ? ScriptEngine::CollisionCallback::TriggerEnter : ScriptEngine::CollisionCallback::TriggerExit )
        : ( enter ? ScriptEngine::CollisionCallback::CollisionEnter : ScriptEngine::CollisionCallback::CollisionExit );
    // Each side hears about the other; the normal points from the receiver towards the other.
    DeliverCollision( collision.A, collision.B, static_cast<int>( kind ), collision.Point, collision.Normal );
    DeliverCollision( collision.B, collision.A, static_cast<int>( kind ), collision.Point, collision.Normal * -1.f );
#endif
}


void ScriptCore::DeliverCollision( const EntityHandle& InSelf, const EntityHandle& InOther, int InKind, const Vector3& InPoint, const Vector3& InNormal )
{
#if USING( ME_SCRIPTING )
    if( !InSelf )
    {
        return;
    }
    // The collider's entity, and the body it belongs to when that's an ancestor (compound bodies).
    Entity* targets[2] = {};
    CollisionRouter::ResolveTargets( *InSelf.Get(), targets );
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
    HavanaUtils::Label( "Scripting" );
    ImGui::TextUnformatted( ScriptEngine::IsAvailable() ? "running" : "unavailable" );
    HavanaUtils::SkipLabel();
    if( ImGui::Button( "Rebuild && Reload Scripts" ) )
    {
        ScriptEngine::RequestReload();
    }
    if( ImGui::TreeNodeEx( "Script Classes", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding ) )
    {
        const int count = ScriptEngine::GetScriptCount();
        for( int i = 0; i < count; ++i )
        {
            ImGui::BulletText( "%s", ScriptEngine::GetScriptName( i ).c_str() );
        }
        ImGui::TreePop();
    }
#endif
}
#endif
