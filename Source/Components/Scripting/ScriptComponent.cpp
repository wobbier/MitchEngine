#include "PCH.h"

#include "ScriptComponent.h"
#include "imgui.h"
#include "Cores/Scripting/ScriptCore.h"
#include "Scripting/ScriptEngine.h"
#include "Components/Transform.h"
#include <mono/metadata/loader.h>
#include <mono/metadata/reflection.h>    // this should go

static std::unordered_map<MonoType*, std::function<bool( EntityHandle )>> s_EntityHasComponentFuncs;

template<typename... Component>
static void RegisterComponent()
{
    ( [] ()
    {
        std::string_view typeName = typeid( Component ).name();
        size_t pos = typeName.find_last_of( ' ' );
        std::string_view structName = typeName.substr( pos + 1 );
        std::string managedTypename( structName );

        MonoType* managedType = mono_reflection_type_from_name( managedTypename.data(), ScriptEngine::GetCoreImage() );
        if ( !managedType )
        {
            YIKES( "Could not find component type {}" );
            return;
        }
        s_EntityHasComponentFuncs[managedType] = [] ( EntityHandle entity ) { return entity->HasComponent<Component>(); };
    }( ), ... );
}

ScriptComponent::ScriptComponent()
    : Component( "ScriptComponent" )
{
    mono_add_internal_call( "Transform::Entity_GetTranslation", Transform_GetTranslation );
    mono_add_internal_call( "Transform::Entity_SetTranslation", Transform_SetTranslation );

    mono_add_internal_call( "Entity::Entity_HasComponent", Entity_HasComponent );
    RegisterComponent<Transform>();
}

ScriptComponent::~ScriptComponent()
{
    delete Instance;
}

void ScriptComponent::Init()
{
    if ( !Instance && !ScriptName.empty() )
    {
        auto foundClass = ScriptEngine::sScriptData.ClassInfo.find( ScriptName );
        if ( foundClass != ScriptEngine::sScriptData.ClassInfo.end() )
        {
            Instance = new ScriptInstance( foundClass->second, Parent );
        }
    }
}

void ScriptComponent::OnEditorInspect()
{
    // Uninitialized script
    if ( !Instance )
    {
        if ( ImGui::BeginCombo( "Script Name", "" ) )
        {
            for ( auto it : ScriptEngine::sScriptData.ClassInfo )
            {
                if ( ImGui::Selectable( it.first.c_str(), false ) )
                {
                    auto foundClass = ScriptEngine::sScriptData.ClassInfo.find( it.first );
                    if ( foundClass != ScriptEngine::sScriptData.ClassInfo.end() )
                    {
                        // create class instance
                        Instance = new ScriptInstance( foundClass->second, Parent );
                        ScriptName = foundClass->first;
                        Instance->OnCreate();
                    }
                    break;
                }
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::Text( ScriptName.c_str() );
    }
}

void ScriptComponent::OnSerialize( json& outJson )
{
    outJson["ScriptName"] = ScriptName;
}

void ScriptComponent::OnDeserialize( const json& inJson )
{
    ScriptName = inJson["ScriptName"];
}

void ScriptComponent::Transform_GetTranslation(EntityID id, Vector3* outPosition)
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    *outPosition = handle->GetComponent<Transform>().GetPosition();
}


void ScriptComponent::Transform_SetTranslation( EntityID id, Vector3* inPos )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    handle->GetComponent<Transform>().SetPosition(*inPos);
}

bool ScriptComponent::Entity_HasComponent( EntityID id, MonoReflectionType* inType )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );

    MonoType* managedType = mono_reflection_type_get_type( inType );
    return s_EntityHasComponentFuncs.at(managedType)( handle );
}