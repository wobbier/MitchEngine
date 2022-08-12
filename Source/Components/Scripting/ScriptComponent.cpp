#include "PCH.h"

#include "ScriptComponent.h"
#include "imgui.h"
#include "Cores/Scripting/ScriptCore.h"
#include "Scripting/ScriptEngine.h"
#include "Components/Transform.h"
#include <mono/metadata/loader.h>

ScriptComponent::ScriptComponent()
    : Component( "ScriptComponent" )
{
    mono_add_internal_call( "Entity::Entity_GetTranslation", Entity_GetTranslation );
    mono_add_internal_call( "Entity::Entity_SetTranslation", Entity_SetTranslation );
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

void ScriptComponent::Entity_GetTranslation(EntityID id, Vector3* outPosition)
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    *outPosition = handle->GetComponent<Transform>().GetPosition();
}


void ScriptComponent::Entity_SetTranslation( EntityID id, Vector3* inPos )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    handle->GetComponent<Transform>().SetPosition(*inPos);
}