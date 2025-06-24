#include "PCH.h"

#if USING( ME_SCRIPTING )
#include <mono/metadata/loader.h>
#include <mono/metadata/reflection.h>    // this should go
#endif

#include "ScriptComponent.h"
#include "imgui.h"
#include "Cores/Scripting/ScriptCore.h"
#include "Scripting/ScriptEngine.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Engine/Engine.h"
#include "Components/UI/BasicUIView.h"
#include "Scripting/MonoUtils.h"
#include <Web\HttpDownload.h>
#include "File.h"
#include <mono\metadata\appdomain.h>


#if USING( ME_SCRIPTING )
static std::unordered_map<MonoType*, std::function<bool( EntityHandle )>> s_EntityHasComponentFuncs;

template<typename... Component>
static void RegisterComponent()
{
    ( []()
        {
            std::string_view typeName = typeid( Component ).name();
            size_t pos = typeName.find_last_of( ' ' );
            std::string_view structName = typeName.substr( pos + 1 );
            std::string managedTypename( structName );

            MonoType* managedType = mono_reflection_type_from_name( managedTypename.data(), ScriptEngine::GetCoreImage() );
            if( !managedType )
            {
                YIKES( "Could not find component type {}" );
                return;
            }
            s_EntityHasComponentFuncs[managedType] = []( EntityHandle entity )
                {
                    return entity->HasComponent<Component>();
                };
        }( ), ... );
}
#endif

ScriptComponent::ScriptComponent()
    : Component( "ScriptComponent" )
{
#if USING( ME_SCRIPTING )
    // Remove this shit
    // Did you update your bgfx .hpp shaders at all??
    mono_add_internal_call( "Transform::Entity_GetTranslation", (void*)Transform_GetTranslation );
    mono_add_internal_call( "Transform::Entity_GetTranslation", (void*)Transform_GetTranslation );
    mono_add_internal_call( "Transform::Transform_GetScale", (void*)Transform_GetScale );
    mono_add_internal_call( "Transform::Transform_SetScale", (void*)Transform_SetScale );
    mono_add_internal_call( "Camera::Camera_GetClearColor", (void*)Camera_GetClearColor );
    mono_add_internal_call( "Camera::Camera_SetClearColor", (void*)Camera_SetClearColor );
    mono_add_internal_call( "Input::IsKeyDown", (void*)Input_IsKeyDown );

    mono_add_internal_call( "Entity::Entity_HasComponent", (void*)Entity_HasComponent );
    mono_add_internal_call( "BasicUIView::BasicUIView_ExecuteJS", (void*)BasicUIView_ExecuteJS );
    mono_add_internal_call( "HTTP::HTTP_DownloadFile", (void*)HTTP_DownloadFile );
    // does this go into the other components?
    RegisterComponent<Transform>();
    RegisterComponent<Camera>();
    RegisterComponent<BasicUIView>();
#endif
}

ScriptComponent::~ScriptComponent()
{
}

void ScriptComponent::Init()
{
#if USING( ME_SCRIPTING )
    if( !Instance && !ScriptName.empty() )
    {
        auto foundClass = ScriptEngine::sScriptData.EntityClasses.find( ScriptName );
        if( foundClass != ScriptEngine::sScriptData.EntityClasses.end() )
        {
            Instance = ScriptEngine::CreateScriptInstance( foundClass->second, Parent );

            if( ScriptEngine::sScriptData.EntityScriptFields.find( Parent.GetID().Value() ) != ScriptEngine::sScriptData.EntityScriptFields.end() )
            {
                const ScriptFieldMap& fieldMap = ScriptEngine::sScriptData.EntityScriptFields.at( Parent.GetID().Value() );
                for( const auto& [name, fieldInstance] : fieldMap )
                {
                    Instance->SetFieldValueInternal( name, (void*)fieldInstance.Buffer );
                }
            }
            ScriptName = foundClass->first;

            // Call OnCreate exactly ONCE after initialization:
            Instance->OnCreate();
        }
    }
#endif
}


#if USING( ME_EDITOR )

void ScriptComponent::OnEditorInspect()
{
#if USING( ME_SCRIPTING )
    // Uninitialized script
    if( !Instance )
    {
        if( ImGui::BeginCombo( "Script Name", "" ) )
        {
            for( auto it : ScriptEngine::sScriptData.EntityClasses )
            {
                if( ImGui::Selectable( it.first.c_str(), false ) )
                {
                    auto foundClass = ScriptEngine::sScriptData.EntityClasses.find( it.first );
                    if( foundClass != ScriptEngine::sScriptData.EntityClasses.end() )
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
        // #TODO: questionable??
        if( true )//if ( static_cast<EditorApp*>( GetEngine().GetGame() )->IsGameRunning() )
        {
            ScriptClass& scriptClass = ScriptEngine::GetEntityClass( ScriptName );
            DrawValues( scriptClass );
        }
        else
        {
            DrawValues( Instance->GetScriptClass() );
        }
    }
#endif
}

#endif

#if USING( ME_SCRIPTING )

void ScriptComponent::DrawValues( const ScriptClass& scriptClass )
{
#if USING( ME_IMGUI )
    const auto& fields = scriptClass.m_fields;

    for( const auto& [name, field] : fields )
    {
        switch( field.Type )
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
            if( ImGui::Checkbox( name.c_str(), &value ) )
            {
                Instance->SetFieldValue<bool>( name, value );
            }
            break;
        }
        case MonoUtils::ScriptFieldType::Long:
        {
            int64_t value = Instance->GetFieldValue<int64_t>( name );
            if( HavanaUtils::Int( name.c_str(), value ) )
            {
                Instance->SetFieldValue<int64_t>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::ULong:
        {
            uint64_t value = Instance->GetFieldValue<uint64_t>( name );
            if( HavanaUtils::UInt( name.c_str(), value ) )
            {
                Instance->SetFieldValue<uint64_t>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Short:
        {
            int16_t value = Instance->GetFieldValue<int16_t>( name );
            if( HavanaUtils::Int( name.c_str(), value ) )
            {
                Instance->SetFieldValue<int16_t>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::UShort:
        {
            uint16_t value = Instance->GetFieldValue<uint16_t>( name );
            if( HavanaUtils::UInt( name.c_str(), value ) )
            {
                Instance->SetFieldValue<uint16_t>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Int:
        {
            int value = Instance->GetFieldValue<int>( name );
            if( HavanaUtils::Int( name.c_str(), value ) )
            {
                Instance->SetFieldValue<int>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::UInt:
        {
            unsigned int value = Instance->GetFieldValue<unsigned int>( name );
            if( HavanaUtils::UInt( name.c_str(), value ) )
            {
                Instance->SetFieldValue<unsigned int>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Double:
        {
            double value = Instance->GetFieldValue<double>( name );
            if( HavanaUtils::Double( name.c_str(), value ) )
            {
                Instance->SetFieldValue<double>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Float:
        {
            float value = Instance->GetFieldValue<float>( name );
            if( HavanaUtils::Float( name, value ) )
            {
                Instance->SetFieldValue<float>( name, value );
            }

            break;
        }
        case MonoUtils::ScriptFieldType::Vector2:
        {
            Vector2 v2 = Instance->GetFieldValue<Vector2>( name );
            if( HavanaUtils::EditableVector( name, v2 ) )
            {
                Instance->SetFieldValue<Vector2>( name, v2 );
            }
            break;
        }
        case MonoUtils::ScriptFieldType::Vector3:
        {
            Vector3 v3 = Instance->GetFieldValue<Vector3>( name );
            if( HavanaUtils::EditableVector3( name, v3 ) )
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
#endif
}
#endif

void ScriptComponent::OnSerialize( json& outJson )
{
    outJson["ScriptName"] = ScriptName;
}

void ScriptComponent::OnDeserialize( const json& inJson )
{
    ScriptName = inJson["ScriptName"];
}

#if USING( ME_SCRIPTING )

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

void ScriptComponent::Transform_GetScale( EntityID id, Vector3* outPosition )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    *outPosition = handle->GetComponent<Transform>().GetScale();
}

void ScriptComponent::Transform_SetScale( EntityID id, Vector3* inPos )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    handle->GetComponent<Transform>().SetScale( *inPos );
}

void ScriptComponent::Camera_GetClearColor( EntityID id, Vector3* outPosition )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    *outPosition = handle->GetComponent<Camera>().ClearColor;
}

void ScriptComponent::Camera_SetClearColor( EntityID id, Vector3* inPos )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    handle->GetComponent<Camera>().ClearColor = *inPos;
}


void ScriptComponent::BasicUIView_ExecuteJS( EntityID id, MonoString* inString )
{
    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );
    if( handle )
    {
        handle->GetComponent<BasicUIView>().ExecuteScript( MonoUtils::MonoStringToUTF8( inString ) );
    }
}


MonoString* ScriptComponent::HTTP_DownloadFile( MonoString* inURL, MonoString* inDirectory )
{
    char* str = mono_string_to_utf8( inURL );
    char* inDirectoryMono = mono_string_to_utf8( inDirectory );
    Path outPath = Path( inDirectoryMono );
    if( Web::DownloadFile( str, outPath ) )
    {
        return mono_string_new( mono_domain_get(), File( outPath ).Read().c_str() );
    }
    return nullptr;
}


bool ScriptComponent::Entity_HasComponent( EntityID id, MonoReflectionType* inType )
{
    if( id.IsNull() )
    {
        return false;
    }

    EntityHandle handle( id, ScriptEngine::sScriptData.worldPtr );

    MonoType* managedType = mono_reflection_type_get_type( inType );
    return s_EntityHasComponentFuncs.at( managedType )( handle );
}

bool ScriptComponent::Input_IsKeyDown( KeyCode key )
{
    return GetEngine().GetInput().IsKeyDown( key );
}

#endif
