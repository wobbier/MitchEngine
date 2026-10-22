#include "PCH.h"

#include "ScriptComponent.h"
#include "imgui.h"
#include "Cores/Scripting/ScriptCore.h"
#include "Scripting/ScriptEngine.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Engine/Engine.h"
#include "Components/UI/BasicUIView.h"
#include <Web/HttpDownload.h>
#include "File.h"
#include "Utils/HavanaUtils.h"
#include "Engine/World.h"

ScriptComponent::ScriptComponent()
    : Component( "ScriptComponent" )
{
}


ScriptComponent::~ScriptComponent()
{
}


void ScriptComponent::Init()
{
#if USING( ME_SCRIPTING )
    if( m_dotnetHandle < 0 && !ScriptName.empty() )
    {
        m_dotnetHandle = ScriptEngine::CreateScript( ScriptName, Parent.GetID() );
        if( m_dotnetHandle >= 0 )
        {
            // restore inspector/serialized values before the script runs
            if( !m_savedFields.empty() )
                ScriptEngine::SetFieldsJson( m_dotnetHandle, m_savedFields );
            ScriptEngine::ScriptOnStart( m_dotnetHandle );
        }
        else
        {
            BRUH_NEW( "dotnet: couldn't create script '{}', is it in the game dll?", ScriptName );
        }
    }
#endif
}


#if USING( ME_EDITOR )

void ScriptComponent::OnEditorInspect()
{
#if USING( ME_SCRIPTING )
    if( m_dotnetHandle < 0 )
    {
        int count = ScriptEngine::GetScriptCount();
        if( ImGui::BeginCombo( "Script Name", ScriptName.empty() ? "" : ScriptName.c_str() ) )
        {
            for( int i = 0; i < count; ++i )
            {
                std::string name = ScriptEngine::GetScriptName( i );
                if( ImGui::Selectable( name.c_str(), ScriptName == name ) )
                {
                    ScriptName = name;
                    m_dotnetHandle = ScriptEngine::CreateScript( ScriptName, Parent.GetID() );
                    if( m_dotnetHandle >= 0 )
                    {
                        ScriptEngine::ScriptOnStart( m_dotnetHandle );
                    }
                    else
                    {
                        BRUH_NEW( "dotnet: CreateScript failed for '{}'", ScriptName );
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::Text( "%s (handle: %d)", ScriptName.c_str(), m_dotnetHandle );
        ScriptEngine::ScriptOnEditorInspect( m_dotnetHandle );
    }
#endif
}

#endif

void ScriptComponent::OnSerialize( json& outJson )
{
    outJson["ScriptName"] = ScriptName;

#if USING( ME_SCRIPTING )
    // serialize script for saving
    if( m_dotnetHandle >= 0 )
    {
        m_savedFields = ScriptEngine::GetFieldsJson( m_dotnetHandle );
    }

    if( !m_savedFields.empty() )
    {
        outJson["ScriptFields"] = m_savedFields;
    }
#endif
}


void ScriptComponent::OnDeserialize( const json& inJson )
{
    ScriptName = inJson["ScriptName"];

#if USING( ME_SCRIPTING )
    if( inJson.contains( "ScriptFields" ) )
    {
        m_savedFields = inJson["ScriptFields"].get<std::string>();
    }
#endif
}