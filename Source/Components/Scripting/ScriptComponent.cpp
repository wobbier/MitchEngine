#include "PCH.h"

#if USING( ME_SCRIPTING )

#include "ScriptComponent.h"
#include "imgui.h"
#include "Cores/Scripting/ScriptCore.h"
#include "Scripting/ScriptEngine.h"
#include "Components/Transform.h"
#include <mono/metadata/loader.h>
#include <mono/metadata/reflection.h>    // this should go
#include "Engine/Engine.h"

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
    mono_add_internal_call( "Input::IsKeyDown", Input_IsKeyDown );

    mono_add_internal_call( "Entity::Entity_HasComponent", Entity_HasComponent );
    RegisterComponent<Transform>();
}

ScriptComponent::~ScriptComponent()
{
}

void ScriptComponent::Init()
{
    if ( !Instance && !ScriptName.empty() )
    {
        auto foundClass = ScriptEngine::sScriptData.EntityClasses.find( ScriptName );
        if ( foundClass != ScriptEngine::sScriptData.EntityClasses.end() )
        {
            Instance = ScriptEngine::CreateScriptInstance( foundClass->second, Parent );

            if ( ScriptEngine::sScriptData.EntityScriptFields.find( Parent.GetID().Value() ) != ScriptEngine::sScriptData.EntityScriptFields.end() )
            {
                const ScriptFieldMap& fieldMap = ScriptEngine::sScriptData.EntityScriptFields.at( Parent.GetID().Value() );
                for ( const auto& [name, fieldInstance] : fieldMap )
                {
                    Instance->SetFieldValueInternal( name, (void*)fieldInstance.Buffer );
                    ScriptName = foundClass->first;
                    Instance->OnCreate();
                }
            }
        }
    }
}

#if USING( ME_EDITOR )

void ScriptComponent::OnEditorInspect()
{
    // Uninitialized script
    if ( !Instance )
    {
        if ( ImGui::BeginCombo( "Script Name", "" ) )
        {
            for ( auto it : ScriptEngine::sScriptData.EntityClasses )
            {
                if ( ImGui::Selectable( it.first.c_str(), false ) )
                {
                    auto foundClass = ScriptEngine::sScriptData.EntityClasses.find( it.first );
                    if ( foundClass != ScriptEngine::sScriptData.EntityClasses.end() )
                    {
                        // create class instance
                        Instance = ScriptEngine::CreateScriptInstance( foundClass->second, Parent );
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

        // Add a inspect settings option passed in
        if ( true )//if ( static_cast<EditorApp*>( GetEngine().GetGame() )->IsGameRunning() )
        {
            ScriptClass& scriptClass = ScriptEngine::GetEntityClass( ScriptName );
            DrawValues( scriptClass );
        }
        else
        {
            DrawValues( Instance->GetScriptClass() );
        }
    }
}

#endif

void ScriptComponent::DrawValues( const ScriptClass& scriptClass )
{
    const auto& fields = scriptClass.m_fields;

    for ( const auto& [name, field] : fields )
    {
        switch ( field.Type )
        {
        case MonoUtils::ScriptFieldType::Char:
        {
            char value = Instance->GetFieldValue<char>( name );
            ImGui::LabelText( name.c_str(), &value );

            break;
        }
        case MonoUtils::ScriptFieldType::Bool:
        {
            bool value = Instance->GetFieldValue<bool>( name );
            if ( ImGui::Checkbox( name.c_str(), &value ) )
            {
                Instance->SetFieldValue<bool>( name, value );
            }
            break;
        }
        case MonoUtils::ScriptFieldType::Long:
        {
            int64_t value = Instance->GetFieldValue<int64_t>( name );
            if ( HavanaUtils::Int( name.c_str(), value ) )
            {
                Instance->SetFieldValue<int64_t>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::ULong:
        {
            uint64_t value = Instance->GetFieldValue<uint64_t>( name );
            if ( HavanaUtils::UInt( name.c_str(), value ) )
            {
                Instance->SetFieldValue<uint64_t>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Short:
        {
            int16_t value = Instance->GetFieldValue<int16_t>( name );
            if ( HavanaUtils::Int( name.c_str(), value ) )
            {
                Instance->SetFieldValue<int16_t>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::UShort:
        {
            uint16_t value = Instance->GetFieldValue<uint16_t>( name );
            if ( HavanaUtils::UInt( name.c_str(), value ) )
            {
                Instance->SetFieldValue<uint16_t>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Int:
        {
            int value = Instance->GetFieldValue<int>( name );
            if ( HavanaUtils::Int( name.c_str(), value ) )
            {
                Instance->SetFieldValue<int>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::UInt:
        {
            unsigned int value = Instance->GetFieldValue<unsigned int>( name );
            if ( HavanaUtils::UInt( name.c_str(), value ) )
            {
                Instance->SetFieldValue<unsigned int>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Double:
        {
            double value = Instance->GetFieldValue<double>( name );
            if ( HavanaUtils::Double( name.c_str(), value ) )
            {
                Instance->SetFieldValue<double>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Float:
        {
            float value = Instance->GetFieldValue<float>( name );
            if ( HavanaUtils::Float( name, value ) )
            {
                Instance->SetFieldValue<float>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Vector2:
        {
            Vector2 v2 = Instance->GetFieldValue<Vector2>( name );
            if ( HavanaUtils::EditableVector( name, v2 ) )
            {
                Instance->SetFieldValue<Vector2>( name, v2 );
            }
            break;
        }
        case MonoUtils::ScriptFieldType::Vector3:
        {
            Vector3 v3 = Instance->GetFieldValue<Vector3>( name );
            if ( HavanaUtils::EditableVector3( name, v3 ) )
            {
                Instance->SetFieldValue<Vector3>( name, v3 );
            }
            break;
        }
        default:
            BRUH_FMT( "Missing entry for %s", name.c_str() );
            break;
        }
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

void ScriptComponent::Transform_GetTranslation( EntityID id, Vector3* outPosition )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    *outPosition = handle->GetComponent<Transform>().GetPosition();
}


void ScriptComponent::Transform_SetTranslation( EntityID id, Vector3* inPos )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    handle->GetComponent<Transform>().SetPosition( *inPos );
}

bool ScriptComponent::Entity_HasComponent( EntityID id, MonoReflectionType* inType )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );

    MonoType* managedType = mono_reflection_type_get_type( inType );
    return s_EntityHasComponentFuncs.at( managedType )( handle );
}

bool ScriptComponent::Input_IsKeyDown( KeyCode key )
{
    return GetEngine().GetInput().IsKeyDown( key );
}

#endif
