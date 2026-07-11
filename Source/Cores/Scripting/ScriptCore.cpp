#include "PCH.h"

#include "ScriptCore.h"
#include "Components/Scripting/ScriptComponent.h"

#include "File.h"
#include "Utils/PlatformUtils.h"
#include "Scripting/ScriptEngine.h"
#include "Events/SceneEvents.h"
#include "ECS/Core.h"


ScriptCore::ScriptCore()
    : Base( ComponentFilter().Requires<ScriptComponent>() )
{
    SetIsSerializable( true );
    EventManager::GetInstance().RegisterReceiver( this, { SceneLoadedEvent::GetEventId() } );

#if USING( ME_SCRIPTING )
    ScriptEngine::Init();
#endif
}


ScriptCore::~ScriptCore()
{
}


void ScriptCore::Init()
{
}


void ScriptCore::Update( const UpdateContext& inUpdateContext )
{
#if USING( ME_SCRIPTING )
    OPTICK_EVENT( "ScriptCore::Update" );
    for( auto& entity : GetEntities() )
    {
        auto& comp = entity.GetComponent<ScriptComponent>();
        if( comp.m_dotnetHandle >= 0 )
        {
            ScriptEngine::ScriptOnUpdate( comp.m_dotnetHandle, inUpdateContext.GetDeltaTime() );
        }
    }
#endif
}


void ScriptCore::LateUpdate( const UpdateContext& inUpdateContext )
{

}


void ScriptCore::OnEntityAdded( Entity& NewEntity )
{
    // nothing to do - ScriptComponent::Init() already runs CreateScript + ScriptOnStart.
    // #TODO: add a ScriptOnCreate binding here if we ever want an Awake-before-Start split.
}


void ScriptCore::OnEntityRemoved( Entity& InEntity )
{
#if USING( ME_SCRIPTING )
    auto& comp = InEntity.GetComponent<ScriptComponent>();
    if( comp.m_dotnetHandle >= 0 )
    {
        ScriptEngine::ScriptOnDestroy( comp.m_dotnetHandle );
        comp.m_dotnetHandle = -1;
    }
#endif
}


#if USING( ME_EDITOR )

void ScriptCore::OnEditorInspect()
{
#if USING( ME_SCRIPTING )
    OPTICK_EVENT( "ScriptCore::OnEditorInspect" );

    // debug: dump the scriptable classes the game dll handed us.
    // #TODO: the old "All Classes" dump needs a new binding to come back.
    if( ImGui::CollapsingHeader( "Entity Classes" ) )
    {
        int count = ScriptEngine::GetScriptCount();
        for( int i = 0; i < count; ++i )
        {
            std::string name = ScriptEngine::GetScriptName( i );
            ImGui::Text( "%s", name.c_str() );
        }
    }
#endif
}

#endif

bool ScriptCore::OnEvent( const BaseEvent& evt )
{
    return false;
}

