#include "PCH.h"

#if USING( ME_SCRIPTING )

#include "ScriptCore.h"
#include "Components/Scripting/ScriptComponent.h"
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/attrdefs.h>
#include "Math/Vector3.h"

#include "Path.h"
#include "imgui.h"
#include "File.h"
#include "Utils/PlatformUtils.h"
#include "Scripting/MonoUtils.h"
#include "Scripting/ScriptEngine.h"
#include "Events/SceneEvents.h"
#include "ECS/Core.h"



ScriptCore::ScriptCore()
    : Base( ComponentFilter().Requires<ScriptComponent>() )
{
    SetIsSerializable( true );
    EventManager::GetInstance().RegisterReceiver( this, { SceneLoadedEvent::GetEventId() } );
    ScriptEngine::Init();
}

ScriptCore::~ScriptCore()
{
}


void ScriptCore::Init()
{
}

void ScriptCore::Update( const UpdateContext& inUpdateContext )
{
    auto& entities = GetEntities();
    for ( auto& InEntity : entities )
    {
        auto& scriptComponent = InEntity.GetComponent<ScriptComponent>();

        if ( scriptComponent.Instance )
        {
            scriptComponent.Instance->OnUpdate( inUpdateContext.GetDeltaTime() );
        }
    }
}

void ScriptCore::LateUpdate( const UpdateContext& inUpdateContext )
{

}

void ScriptCore::OnEntityAdded( Entity& NewEntity )
{
    ScriptComponent& comp = NewEntity.GetComponent<ScriptComponent>();
    if ( comp.Instance )
    {
        // could be possibly called before other entities are done
        comp.Instance->OnCreate();
    }
}

void ScriptCore::OnEntityRemoved( Entity& InEntity )
{

}

#if USING( ME_EDITOR )

void ScriptCore::OnEditorInspect()
{
    //float value;
    //mono_field_get_value( ScriptEngine::testClassInstance.ClassObject, ScriptEngine::floatField, &value );
    //ImGui::DragFloat( "C# Float", &value );
    //mono_field_set_value( ScriptEngine::testClassInstance.ClassObject, ScriptEngine::floatField, &value );
    //
    //ScriptEngine::testClassInstance.InvokeFull("PrintFloatVar");

    for ( auto& it : ScriptEngine::LoadedClasses )
    {
        ImGui::Text( "%s %s", it.Namespace.c_str(), it.Name.c_str() );
    }
}

#endif

bool ScriptCore::OnEvent( const BaseEvent& evt )
{
    return false;
}

#endif
