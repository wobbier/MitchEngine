#include "PCH.h"
#include "Gameplay.bindings.h"

#if USING( ME_SCRIPTING )

#include "CLog.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Cores/AudioCore.h"
#include "Cores/PhysicsCore.h"
#include "Cores/SceneCore.h"
#include "Debug/DebugDraw.h"
#include "ECS/Entity.h"
#include "Engine/Engine.h"
#include "Engine/Input.h"
#include "Engine/World.h"
#include "Events/SceneEvents.h"
#include "Path.h"
#include "Scripting/Bindings/BindingContext.h"
#include "World/SceneSerializer.h"
#include <algorithm>
#include <cstring>

using ScriptBindings::MakeHandle;

namespace
{
    const char* Text( const uint8_t* InText )
    {
        return InText ? reinterpret_cast<const char*>( InText ) : "";
    }


    Transform* TransformOf( EntityID InId )
    {
        EntityHandle handle = MakeHandle( InId );
        return handle ? handle->TryGetComponent<Transform>() : nullptr;
    }


    Rigidbody* RigidbodyOf( EntityID InId )
    {
        EntityHandle handle = MakeHandle( InId );
        return handle ? handle->TryGetComponent<Rigidbody>() : nullptr;
    }


    CharacterController* CharacterOf( EntityID InId )
    {
        EntityHandle handle = MakeHandle( InId );
        return handle ? handle->TryGetComponent<CharacterController>() : nullptr;
    }
}

// Logging

static void Eng_LogWarning( const uint8_t* inMessage )
{
    CLog::Log( CLog::LogType::Warning, Text( inMessage ) );
}


static void Eng_LogError( const uint8_t* inMessage )
{
    CLog::Log( CLog::LogType::Error, Text( inMessage ) );
}

// Time

static float Eng_Time_GetDeltaTime()
{
    return GetEngine().DeltaTime;
}


static float Eng_Time_GetUnscaledDeltaTime()
{
    return GetEngine().UnscaledDeltaTime;
}


static float Eng_Time_GetFixedDeltaTime()
{
    return GetEngine().GetFixedTimeStep();
}


static float Eng_Time_GetTimeScale()
{
    return GetEngine().GetTimeScale();
}


static void Eng_Time_SetTimeScale( float inScale )
{
    GetEngine().SetTimeScale( inScale );
}

// Entity lifecycle

static void Eng_Entity_Destroy( EntityID inId )
{
    if( EntityHandle handle = MakeHandle( inId ) )
    {
        handle->MarkForDelete();   // with its children, at the end of the frame
    }
}


static bool Eng_Entity_IsActive( EntityID inId )
{
    EntityHandle handle = MakeHandle( inId );
    return handle && handle->IsActiveSelf();
}


static void Eng_Entity_SetActive( EntityID inId, bool inActive )
{
    if( EntityHandle handle = MakeHandle( inId ) )
    {
        handle->SetActive( inActive );
    }
}


static int Eng_Entity_GetName( EntityID inId, uint8_t* outName, int inSize )
{
    EntityHandle handle = MakeHandle( inId );
    const std::string name = handle ? handle->GetName() : std::string();
    if( outName && inSize > 0 )
    {
        const size_t length = std::min( name.size(), static_cast<size_t>( inSize - 1 ) );
        std::memcpy( outName, name.data(), length );
        outName[length] = 0;
    }
    return static_cast<int>( name.size() );
}


static void Eng_Entity_SetName( EntityID inId, const uint8_t* inName )
{
    if( EntityHandle handle = MakeHandle( inId ) )
    {
        handle->SetName( Text( inName ) );
    }
}

// Transform hierarchy and world space

static void Eng_Transform_GetWorldPosition( EntityID inId, Vector3* outPosition )
{
    Transform* transform = TransformOf( inId );
    *outPosition = transform ? transform->GetWorldPosition() : Vector3();
}


static void Eng_Transform_SetWorldPosition( EntityID inId, const Vector3* inPosition )
{
    if( Transform* transform = TransformOf( inId ) )
    {
        transform->SetWorldPosition( *inPosition );
    }
}


static void Eng_Transform_GetForward( EntityID inId, Vector3* outForward )
{
    Transform* transform = TransformOf( inId );
    *outForward = transform ? transform->Front() : Vector3::Front;
}


static void Eng_Transform_GetRight( EntityID inId, Vector3* outRight )
{
    Transform* transform = TransformOf( inId );
    *outRight = transform ? transform->Right() : Vector3::Right;
}


static void Eng_Transform_GetUp( EntityID inId, Vector3* outUp )
{
    Transform* transform = TransformOf( inId );
    *outUp = transform ? transform->Up() : Vector3::Up;
}


static void Eng_Transform_LookAt( EntityID inId, const Vector3* inTarget )
{
    if( Transform* transform = TransformOf( inId ) )
    {
        transform->LookAt( *inTarget );
    }
}


static void Eng_Transform_GetParent( EntityID inId, EntityID* outParent )
{
    *outParent = EntityID{};
    Transform* transform = TransformOf( inId );
    Transform* parent = transform ? transform->GetParentTransform() : nullptr;
    if( parent && parent->Parent && parent != GetEngine().SceneNodes->GetRootTransform() )
    {
        *outParent = parent->Parent.GetID();
    }
}


static void Eng_Transform_SetParent( EntityID inId, EntityID inParent )
{
    Transform* transform = TransformOf( inId );
    if( !transform )
    {
        return;
    }
    Transform* parent = TransformOf( inParent );
    // Keeps the world pose, like reparenting in the editor.
    transform->SetParent( parent ? *parent : *GetEngine().SceneNodes->GetRootTransform(), true );
}

// World and scenes

static void Eng_World_Instantiate( const uint8_t* inPrefab, EntityID inParent, EntityID* outId )
{
    *outId = EntityID{};
    auto world = ScriptBindings::GetWorld().lock();
    if( !world )
    {
        return;
    }
    EntityHandle instance = SceneSerializer::InstantiatePrefab( *world, Text( inPrefab ), TransformOf( inParent ) );
    if( instance )
    {
        *outId = instance.GetID();
    }
}


static void Eng_World_LoadScene( const uint8_t* inScene )
{
    // Queued: the current frame finishes on the scene the script lives in.
    LoadSceneEvent event;
    event.Level = Text( inScene );
    event.Queue();
}

// Additive scenes

static int Eng_World_LoadSceneAdditive( const uint8_t* inScene, bool inAsync )
{
    return GetEngine().LoadSceneAdditive( Text( inScene ), inAsync );
}


static bool Eng_World_UnloadScene( int inScene )
{
    return inScene > 0 && inScene <= UINT16_MAX && GetEngine().UnloadAdditiveScene( static_cast<uint16_t>( inScene ) );
}


static int Eng_World_GetSceneState( int inScene )
{
    if( inScene <= 0 || inScene > UINT16_MAX )
    {
        return 0;
    }
    return static_cast<int>( GetEngine().GetSceneStreaming().GetState( static_cast<uint16_t>( inScene ) ) );
}

// Input actions

static float Eng_Input_GetActionValue( const uint8_t* inAction )
{
    return GetEngine().GetInput().GetAction( Text( inAction ) ).GetValue();
}


static void Eng_Input_GetActionVector2( const uint8_t* inAction, Vector2* outValue )
{
    *outValue = GetEngine().GetInput().GetAction( Text( inAction ) ).GetVector2();
}


static bool Eng_Input_IsActionPressed( const uint8_t* inAction )
{
    return GetEngine().GetInput().GetAction( Text( inAction ) ).IsPressed();
}


static bool Eng_Input_WasActionPressed( const uint8_t* inAction )
{
    return GetEngine().GetInput().GetAction( Text( inAction ) ).WasPressed();
}


static bool Eng_Input_WasActionReleased( const uint8_t* inAction )
{
    return GetEngine().GetInput().GetAction( Text( inAction ) ).WasReleased();
}


static int Eng_Input_GetPlayerCount()
{
    return GetEngine().GetInput().GetPlayers().GetPlayerCount();
}


static float Eng_Input_GetPlayerActionValue( int inPlayer, const uint8_t* inAction )
{
    return GetEngine().GetInput().GetAction( inPlayer, Text( inAction ) ).GetValue();
}


static void Eng_Input_GetPlayerActionVector2( int inPlayer, const uint8_t* inAction, Vector2* outValue )
{
    *outValue = GetEngine().GetInput().GetAction( inPlayer, Text( inAction ) ).GetVector2();
}


static bool Eng_Input_IsPlayerActionPressed( int inPlayer, const uint8_t* inAction )
{
    return GetEngine().GetInput().GetAction( inPlayer, Text( inAction ) ).IsPressed();
}


static bool Eng_Input_WasPlayerActionPressed( int inPlayer, const uint8_t* inAction )
{
    return GetEngine().GetInput().GetAction( inPlayer, Text( inAction ) ).WasPressed();
}


static bool Eng_Input_WasPlayerActionReleased( int inPlayer, const uint8_t* inAction )
{
    return GetEngine().GetInput().GetAction( inPlayer, Text( inAction ) ).WasReleased();
}


static int Eng_Input_GetPlayerGamepad( int inPlayer )
{
    return GetEngine().GetInput().GetPlayers().GetGamepad( inPlayer );
}


static void Eng_Input_SetJoining( int inMaxPlayers )
{
    InputPlayers& players = GetEngine().GetInput().GetPlayers();
    if( inMaxPlayers > 0 )
    {
        players.EnableJoining( inMaxPlayers );
    }
    else
    {
        players.DisableJoining();
    }
}


static bool Eng_Input_WasKeyPressed( int inKey )
{
    return GetEngine().GetInput().WasKeyPressed( static_cast<KeyCode>( inKey ) );
}


static void Eng_Input_GetMouseDelta( Vector2* outDelta )
{
    *outDelta = GetEngine().GetInput().GetDeviceState().MouseDelta;
}

// Audio

static void Eng_Audio_PlayOneShot( const uint8_t* inClip, float inVolume )
{
    if( AudioCore* audio = AudioCore::Get() )
    {
        AudioPlayParams params;
        params.Volume = inVolume;
        audio->PlayOneShot( Path( Text( inClip ) ), params );
    }
}


static void Eng_Audio_PlayOneShotAt( const uint8_t* inClip, const Vector3* inPosition, float inVolume )
{
    if( AudioCore* audio = AudioCore::Get() )
    {
        AudioPlayParams params;
        params.Volume = inVolume;
        params.SpatialBlend = 1.f;
        params.Position = *inPosition;
        audio->PlayOneShot( Path( Text( inClip ) ), params );
    }
}

// Physics

static bool Eng_Physics_Raycast( const Vector3* inOrigin, const Vector3* inDirection, float inMaxDistance, ScriptRaycastHit* outHit )
{
    *outHit = ScriptRaycastHit();
    PhysicsCore* physics = GetEngine().Physics;
    RaycastHit hit;
    if( !physics || !physics->Raycast( *inOrigin, *inDirection, inMaxDistance, hit ) )
    {
        return false;
    }
    outHit->Entity = hit.Entity ? hit.Entity.GetID() : EntityID{};
    outHit->Point = hit.Position;
    outHit->Normal = hit.Normal;
    outHit->Distance = hit.Distance;
    return true;
}


static void Eng_Rigidbody_AddForce( EntityID inId, const Vector3* inForce, int inMode )
{
    if( Rigidbody* body = RigidbodyOf( inId ) )
    {
        body->AddForce( *inForce, static_cast<ForceMode>( std::clamp( inMode, 0, 3 ) ) );
    }
}


static void Eng_Rigidbody_GetVelocity( EntityID inId, Vector3* outVelocity )
{
    Rigidbody* body = RigidbodyOf( inId );
    *outVelocity = body ? body->GetVelocity() : Vector3();
}


static void Eng_Rigidbody_SetVelocity( EntityID inId, const Vector3* inVelocity )
{
    if( Rigidbody* body = RigidbodyOf( inId ) )
    {
        body->SetVelocity( *inVelocity );
    }
}

// Debug draw

static void Eng_Debug_DrawLine( const Vector3* inFrom, const Vector3* inTo, const Vector3* inColor, float inDuration )
{
    DebugDraw::Line( *inFrom, *inTo, Vector4( inColor->x, inColor->y, inColor->z, 1.f ), inDuration );
}


static void Eng_Debug_DrawSphere( const Vector3* inCenter, float inRadius, const Vector3* inColor, float inDuration )
{
    DebugDraw::Sphere( *inCenter, inRadius, Vector4( inColor->x, inColor->y, inColor->z, 1.f ), inDuration );
}


// Components by name

namespace
{
    BaseComponent* ComponentOf( EntityID InId, const uint8_t* InType )
    {
        EntityHandle handle = MakeHandle( InId );
        return handle ? handle->GetComponentByName( Text( InType ) ) : nullptr;
    }


    // "Color.0" / "Points[2].x" -> "/Color/0" / "/Points/2/x" (for components without reflection data).
    json::json_pointer ToPointer( const std::string& InPath )
    {
        std::string pointer = "/";
        for( char c : InPath )
        {
            if( c != ']' )
            {
                pointer += ( c == '.' || c == '[' ) ? '/' : c;
            }
        }
        return json::json_pointer( pointer );
    }
}


static int Eng_Component_GetField( EntityID inId, const uint8_t* inComponent, const uint8_t* inPath, uint8_t* outJson, int inSize )
{
    BaseComponent* component = ComponentOf( inId, inComponent );
    if( !component )
    {
        return -1;
    }
    const std::string path = Text( inPath );
    json value;
    if( const Reflection::TypeInfo* type = component->GetTypeInfo() )
    {
        value = Reflection::GetPathJson( *type, component->GetReflectedObject(), path );
    }
    if( value.is_null() )
    {
        // Hand-serialized components: read the field from their JSON.
        json all;
        component->Serialize( all );
        const json::json_pointer pointer = ToPointer( path );
        if( all.contains( pointer ) )
        {
            value = all[pointer];
        }
    }
    if( value.is_null() )
    {
        return -1;
    }
    const std::string text = value.dump();
    if( outJson && inSize > 0 )
    {
        const size_t count = std::min( text.size(), static_cast<size_t>( inSize - 1 ) );
        std::memcpy( outJson, text.data(), count );
        outJson[count] = 0;
    }
    return static_cast<int>( text.size() );
}


static bool Eng_Component_SetField( EntityID inId, const uint8_t* inComponent, const uint8_t* inPath, const uint8_t* inJson )
{
    BaseComponent* component = ComponentOf( inId, inComponent );
    const json value = json::parse( Text( inJson ), nullptr, false );
    if( !component || value.is_discarded() )
    {
        return false;
    }
    const std::string path = Text( inPath );
    const std::string field = path.substr( 0, path.find_first_of( ".[" ) );
    bool written = false;
    if( const Reflection::TypeInfo* type = component->GetTypeInfo() )
    {
        written = Reflection::SetPathJson( *type, component->GetReflectedObject(), path, value );
    }
    if( !written )
    {
        json all;
        component->Serialize( all );
        const json::json_pointer pointer = ToPointer( path );
        if( !all.contains( pointer ) )
        {
            return false;
        }
        all[pointer] = value;
        component->Deserialize( all );
    }
    component->OnPropertyChanged( field );
    return true;
}


// Character controller

static void Eng_Character_SetMoveInput( EntityID inId, const Vector3* inDirection )
{
    if( CharacterController* character = CharacterOf( inId ) )
    {
        character->SetMoveInput( *inDirection );
    }
}


static void Eng_Character_Move( EntityID inId, const Vector3* inDisplacement )
{
    if( CharacterController* character = CharacterOf( inId ) )
    {
        character->Move( *inDisplacement );
    }
}


static void Eng_Character_Jump( EntityID inId )
{
    if( CharacterController* character = CharacterOf( inId ) )
    {
        character->Jump();
    }
}


static bool Eng_Character_IsGrounded( EntityID inId )
{
    CharacterController* character = CharacterOf( inId );
    return character && character->IsOnGround();
}


static void Eng_Character_GetVelocity( EntityID inId, Vector3* outVelocity )
{
    CharacterController* character = CharacterOf( inId );
    *outVelocity = character ? character->GetVelocity() : Vector3();
}


static float Eng_Character_GetMaxSpeed( EntityID inId )
{
    CharacterController* character = CharacterOf( inId );
    return character ? character->MaxSpeed : 0.f;
}


static void Eng_Character_SetMaxSpeed( EntityID inId, float inSpeed )
{
    if( CharacterController* character = CharacterOf( inId ) )
    {
        character->MaxSpeed = std::max( inSpeed, 0.f );
    }
}


void Register_GameplayBindings( ScriptEngineAPI& inAPI )
{
    ScriptBindings::RegisterComponent<Rigidbody>( "Rigidbody" );
    ScriptBindings::RegisterComponent<CharacterController>( "CharacterController" );
    inAPI.LogWarning = Eng_LogWarning;
    inAPI.LogError = Eng_LogError;
    inAPI.Time_GetDeltaTime = Eng_Time_GetDeltaTime;
    inAPI.Time_GetUnscaledDeltaTime = Eng_Time_GetUnscaledDeltaTime;
    inAPI.Time_GetFixedDeltaTime = Eng_Time_GetFixedDeltaTime;
    inAPI.Time_GetTimeScale = Eng_Time_GetTimeScale;
    inAPI.Time_SetTimeScale = Eng_Time_SetTimeScale;
    inAPI.Entity_Destroy = Eng_Entity_Destroy;
    inAPI.Entity_IsActive = Eng_Entity_IsActive;
    inAPI.Entity_SetActive = Eng_Entity_SetActive;
    inAPI.Entity_GetName = Eng_Entity_GetName;
    inAPI.Entity_SetName = Eng_Entity_SetName;
    inAPI.Transform_GetWorldPosition = Eng_Transform_GetWorldPosition;
    inAPI.Transform_SetWorldPosition = Eng_Transform_SetWorldPosition;
    inAPI.Transform_GetForward = Eng_Transform_GetForward;
    inAPI.Transform_GetRight = Eng_Transform_GetRight;
    inAPI.Transform_GetUp = Eng_Transform_GetUp;
    inAPI.Transform_LookAt = Eng_Transform_LookAt;
    inAPI.Transform_GetParent = Eng_Transform_GetParent;
    inAPI.Transform_SetParent = Eng_Transform_SetParent;
    inAPI.World_Instantiate = Eng_World_Instantiate;
    inAPI.World_LoadScene = Eng_World_LoadScene;
    inAPI.World_LoadSceneAdditive = Eng_World_LoadSceneAdditive;
    inAPI.World_UnloadScene = Eng_World_UnloadScene;
    inAPI.World_GetSceneState = Eng_World_GetSceneState;
    inAPI.Input_GetActionValue = Eng_Input_GetActionValue;
    inAPI.Input_GetActionVector2 = Eng_Input_GetActionVector2;
    inAPI.Input_IsActionPressed = Eng_Input_IsActionPressed;
    inAPI.Input_WasActionPressed = Eng_Input_WasActionPressed;
    inAPI.Input_WasActionReleased = Eng_Input_WasActionReleased;
    inAPI.Input_WasKeyPressed = Eng_Input_WasKeyPressed;
    inAPI.Input_GetMouseDelta = Eng_Input_GetMouseDelta;
    inAPI.Input_GetPlayerCount = Eng_Input_GetPlayerCount;
    inAPI.Input_GetPlayerActionValue = Eng_Input_GetPlayerActionValue;
    inAPI.Input_GetPlayerActionVector2 = Eng_Input_GetPlayerActionVector2;
    inAPI.Input_IsPlayerActionPressed = Eng_Input_IsPlayerActionPressed;
    inAPI.Input_WasPlayerActionPressed = Eng_Input_WasPlayerActionPressed;
    inAPI.Input_WasPlayerActionReleased = Eng_Input_WasPlayerActionReleased;
    inAPI.Input_GetPlayerGamepad = Eng_Input_GetPlayerGamepad;
    inAPI.Input_SetJoining = Eng_Input_SetJoining;
    inAPI.Audio_PlayOneShot = Eng_Audio_PlayOneShot;
    inAPI.Audio_PlayOneShotAt = Eng_Audio_PlayOneShotAt;
    inAPI.Physics_Raycast = Eng_Physics_Raycast;
    inAPI.Rigidbody_AddForce = Eng_Rigidbody_AddForce;
    inAPI.Rigidbody_GetVelocity = Eng_Rigidbody_GetVelocity;
    inAPI.Rigidbody_SetVelocity = Eng_Rigidbody_SetVelocity;
    inAPI.Debug_DrawLine = Eng_Debug_DrawLine;
    inAPI.Debug_DrawSphere = Eng_Debug_DrawSphere;
    inAPI.Component_GetField = Eng_Component_GetField;
    inAPI.Component_SetField = Eng_Component_SetField;
    inAPI.Character_SetMoveInput = Eng_Character_SetMoveInput;
    inAPI.Character_Move = Eng_Character_Move;
    inAPI.Character_Jump = Eng_Character_Jump;
    inAPI.Character_IsGrounded = Eng_Character_IsGrounded;
    inAPI.Character_GetVelocity = Eng_Character_GetVelocity;
    inAPI.Character_GetMaxSpeed = Eng_Character_GetMaxSpeed;
    inAPI.Character_SetMaxSpeed = Eng_Character_SetMaxSpeed;
}

#endif
