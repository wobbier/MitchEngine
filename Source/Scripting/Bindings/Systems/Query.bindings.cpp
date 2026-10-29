#include "PCH.h"
#include "Query.bindings.h"

#if USING( ME_SCRIPTING )

#include "Components/Animation/Animator.h"
#include "Components/Audio/AudioSource.h"
#include "Components/Effects/ParticleSystem.h"
#include "Components/Transform.h"
#include "Components/Physics/Rigidbody2D.h"
#include "Cores/Physics2DCore.h"
#include "Cores/PhysicsCore.h"
#include "Math/Quaternion.h"
#include "Cores/SceneCore.h"
#include "ECS/ComponentDetail.h"
#include "Engine/Engine.h"
#include "Engine/Input.h"
#include "Engine/World.h"
#include "Scripting/Bindings/BindingContext.h"
#include "Window/IWindow.h"
#include "World/SceneSerializer.h"

#include <cstring>

// Entity queries, application calls, audio sources, particles and masked physics queries for
// scripts, 3D and 2D (CS-5 areas a, b, c, f, g, h and j).

namespace
{
    int Fill( const std::vector<EntityID>& InFound, EntityID* OutEntities, int InMax )
    {
        const int count = std::min( static_cast<int>( InFound.size() ), std::max( InMax, 0 ) );
        for( int i = 0; i < count && OutEntities; ++i )
        {
            OutEntities[i] = InFound[i];
        }
        return static_cast<int>( InFound.size() );
    }


    template<typename Predicate>
    std::vector<EntityID> Collect( Predicate InMatches )
    {
        std::vector<EntityID> found;
        if( SharedPtr<World> world = ScriptBindings::GetWorld().lock() )
        {
            world->ForEachEntity( [&]( Entity& InEntity ) {
                if( InMatches( InEntity ) )
                {
                    found.push_back( InEntity.GetId() );
                }
            } );
        }
        return found;
    }
}


static int Eng_World_FindAllWithComponent( const uint8_t* inComponent, EntityID* outEntities, int inMax )
{
    const std::string name = reinterpret_cast<const char*>( inComponent );
    auto info = FindComponentInfo( name );
    if( info == GetComponentRegistry().end() )
    {
        return 0;
    }
    const TypeId type = info->second.GetTypeFunc();
    return Fill( Collect( [type]( Entity& InEntity ) { return ScriptBindings::HasComponentOfType( InEntity, type ); } ), outEntities, inMax );
}


static int Eng_World_FindAllByName( const uint8_t* inName, EntityID* outEntities, int inMax )
{
    const std::string name = reinterpret_cast<const char*>( inName );
    return Fill( Collect( [&name]( Entity& InEntity ) { return InEntity.GetName() == name; } ), outEntities, inMax );
}


static int Eng_Transform_GetChildren( EntityID inId, EntityID* outEntities, int inMax )
{
    std::vector<EntityID> found;
    EntityHandle handle = ScriptBindings::MakeHandle( inId );
    Transform* transform = handle ? handle->TryGetComponent<Transform>() : nullptr;
    if( transform )
    {
        for( Transform* child : transform->GetChildren() )
        {
            if( child && child->Parent )
            {
                found.push_back( child->Parent.GetID() );
            }
        }
    }
    return Fill( found, outEntities, inMax );
}


static int Eng_Entity_GetGuid( EntityID inId, uint8_t* outGuid, int inSize )
{
    EntityHandle handle = ScriptBindings::MakeHandle( inId );
    const std::string guid = handle ? SceneSerializer::GUIDToString( handle->GetGUID() ) : std::string();
    if( outGuid && inSize > 0 )
    {
        const int length = std::min( static_cast<int>( guid.size() ), inSize - 1 );
        std::memcpy( outGuid, guid.data(), static_cast<size_t>( length ) );
        outGuid[length] = 0;
    }
    return static_cast<int>( guid.size() );
}


static int Eng_Entity_GetLayer( EntityID inId )
{
    EntityHandle handle = ScriptBindings::MakeHandle( inId );
    return handle ? handle->GetLayer() : 0;
}


static void Eng_Entity_SetLayer( EntityID inId, int inLayer )
{
    if( EntityHandle handle = ScriptBindings::MakeHandle( inId ) )
    {
        handle->SetLayer( static_cast<uint8_t>( std::clamp( inLayer, 0, 31 ) ) );
    }
}


static bool Eng_Input_Rumble( int inPlayer, float inLow, float inHigh, float inSeconds )
{
    return GetEngine().GetInput().RumblePlayer( inPlayer, inLow, inHigh, inSeconds );
}


static void Eng_App_Quit()
{
#if USING( ME_EDITOR )
    CLog::Log( CLog::LogType::Info, "App.Quit (ignored in the editor)" );
#else
    GetEngine().Quit();
#endif
}


static void Eng_Window_GetSize( Vector2* outSize )
{
    IWindow* window = GetEngine().GetWindow();
    *outSize = window ? window->GetSize() : Vector2();
}


static int Eng_Window_GetMode()
{
    IWindow* window = GetEngine().GetWindow();
    return window ? static_cast<int>( window->GetWindowMode() ) : 0;
}


static void Eng_Window_SetMode( int inMode )
{
#if USING( ME_EDITOR )
    CLog::Log( CLog::LogType::Info, "Window.Mode (ignored in the editor)" );
#else
    if( IWindow* window = GetEngine().GetWindow() )
    {
        const WindowMode mode = static_cast<WindowMode>( std::clamp( inMode, 0, 2 ) );
        window->SetWindowMode( mode, mode == WindowMode::Windowed ? GetEngine().GetConfig().WindowSize : GetEngine().GetConfig().FullscreenResolution );
    }
#endif
}


namespace
{
    template<typename T>
    T* ComponentOf( EntityID InId )
    {
        EntityHandle handle = ScriptBindings::MakeHandle( InId );
        return handle ? handle->TryGetComponent<T>() : nullptr;
    }
}


static void Eng_AudioSource_Play( EntityID inId )
{
    if( AudioSource* source = ComponentOf<AudioSource>( inId ) ) source->Play();
}


static void Eng_AudioSource_Stop( EntityID inId )
{
    if( AudioSource* source = ComponentOf<AudioSource>( inId ) ) source->Stop();
}


static void Eng_AudioSource_Pause( EntityID inId )
{
    if( AudioSource* source = ComponentOf<AudioSource>( inId ) ) source->Pause();
}


static bool Eng_AudioSource_IsPlaying( EntityID inId )
{
    AudioSource* source = ComponentOf<AudioSource>( inId );
    return source && source->IsPlaying();
}


static void Eng_Particles_Play( EntityID inId )
{
    if( ParticleSystem* particles = ComponentOf<ParticleSystem>( inId ) ) particles->Play();
}


static void Eng_Particles_Stop( EntityID inId )
{
    if( ParticleSystem* particles = ComponentOf<ParticleSystem>( inId ) ) particles->Stop();
}


static void Eng_Particles_Burst( EntityID inId, int inCount )
{
    if( ParticleSystem* particles = ComponentOf<ParticleSystem>( inId ) ) particles->Burst( inCount );
}


static int Eng_Particles_GetCount( EntityID inId )
{
    ParticleSystem* particles = ComponentOf<ParticleSystem>( inId );
    return particles ? static_cast<int>( particles->GetParticleCount() ) : 0;
}


namespace
{
    uint32_t Mask( int InLayerMask )
    {
        return InLayerMask == -1 ? PhysicsLayers::All : static_cast<uint32_t>( InLayerMask );
    }


    void ToScript( const RaycastHit& InHit, ScriptRaycastHit& OutHit )
    {
        OutHit.Entity = InHit.Entity ? InHit.Entity.GetID() : EntityID{};
        OutHit.Point = InHit.Position;
        OutHit.Normal = InHit.Normal;
        OutHit.Distance = InHit.Distance;
    }
}


static bool Eng_Physics_RaycastMasked( const Vector3* inOrigin, const Vector3* inDirection, float inMaxDistance, int inMask, ScriptRaycastHit* outHit )
{
    *outHit = ScriptRaycastHit();
    PhysicsCore* physics = GetEngine().Physics;
    RaycastHit hit;
    if( !physics || !physics->Raycast( *inOrigin, *inDirection, inMaxDistance, hit, Mask( inMask ) ) )
    {
        return false;
    }
    ToScript( hit, *outHit );
    return true;
}


static int Eng_Physics_RaycastAll( const Vector3* inOrigin, const Vector3* inDirection, float inMaxDistance, int inMask, ScriptRaycastHit* outHits, int inMaxHits )
{
    PhysicsCore* physics = GetEngine().Physics;
    if( !physics )
    {
        return 0;
    }
    const std::vector<RaycastHit> hits = physics->RaycastAll( *inOrigin, *inDirection, inMaxDistance, Mask( inMask ) );
    const int count = std::min( static_cast<int>( hits.size() ), std::max( inMaxHits, 0 ) );
    for( int i = 0; i < count && outHits; ++i )
    {
        ToScript( hits[i], outHits[i] );
    }
    return static_cast<int>( hits.size() );
}


static bool Eng_Physics_SphereCast( const Vector3* inOrigin, float inRadius, const Vector3* inDirection, float inMaxDistance, int inMask, ScriptRaycastHit* outHit )
{
    *outHit = ScriptRaycastHit();
    PhysicsCore* physics = GetEngine().Physics;
    RaycastHit hit;
    if( !physics || !physics->SphereCast( *inOrigin, inRadius, *inDirection, inMaxDistance, hit, Mask( inMask ) ) )
    {
        return false;
    }
    ToScript( hit, *outHit );
    return true;
}


static int Eng_Physics_OverlapBox( const Vector3* inCenter, const Vector3* inHalfExtents, const Vector3* inEuler, int inMask, EntityID* outEntities, int inMax )
{
    PhysicsCore* physics = GetEngine().Physics;
    if( !physics )
    {
        return 0;
    }
    std::vector<EntityID> found;
    for( const EntityHandle& entity : physics->OverlapBox( *inCenter, *inHalfExtents, Quaternion::FromEulerDegrees( *inEuler ), Mask( inMask ) ) )
    {
        found.push_back( entity ? entity.GetID() : EntityID{} );
    }
    return Fill( found, outEntities, inMax );
}


namespace
{
    const char* Text( const uint8_t* InText )
    {
        return InText ? reinterpret_cast<const char*>( InText ) : "";
    }
}


static void Eng_Animator_Play( EntityID inId, const uint8_t* inState, float inFade )
{
    if( Animator* animator = ComponentOf<Animator>( inId ) ) animator->Play( Text( inState ), inFade );
}


static void Eng_Animator_Stop( EntityID inId )
{
    if( Animator* animator = ComponentOf<Animator>( inId ) ) animator->Stop();
}


static bool Eng_Animator_IsPlaying( EntityID inId )
{
    Animator* animator = ComponentOf<Animator>( inId );
    return animator && animator->IsPlaying();
}


static void Eng_Animator_SetFloat( EntityID inId, const uint8_t* inName, float inValue )
{
    if( Animator* animator = ComponentOf<Animator>( inId ) ) animator->SetFloat( Text( inName ), inValue );
}


static float Eng_Animator_GetFloat( EntityID inId, const uint8_t* inName )
{
    Animator* animator = ComponentOf<Animator>( inId );
    return animator ? animator->GetFloat( Text( inName ) ) : 0.f;
}


static void Eng_Animator_SetBool( EntityID inId, const uint8_t* inName, bool inValue )
{
    if( Animator* animator = ComponentOf<Animator>( inId ) ) animator->SetBool( Text( inName ), inValue );
}


static bool Eng_Animator_GetBool( EntityID inId, const uint8_t* inName )
{
    Animator* animator = ComponentOf<Animator>( inId );
    return animator && animator->GetBool( Text( inName ) );
}


static void Eng_Animator_SetTrigger( EntityID inId, const uint8_t* inName )
{
    if( Animator* animator = ComponentOf<Animator>( inId ) ) animator->SetTrigger( Text( inName ) );
}


static int Eng_Animator_GetState( EntityID inId, const uint8_t* inLayer, uint8_t* outState, int inSize )
{
    Animator* animator = ComponentOf<Animator>( inId );
    const std::string state = animator ? animator->GetCurrentState( Text( inLayer ) ) : std::string();
    if( outState && inSize > 0 )
    {
        const int length = std::min( static_cast<int>( state.size() ), inSize - 1 );
        std::memcpy( outState, state.data(), static_cast<size_t>( length ) );
        outState[length] = 0;
    }
    return static_cast<int>( state.size() );
}


static float Eng_Animator_GetNormalizedTime( EntityID inId, const uint8_t* inLayer )
{
    Animator* animator = ComponentOf<Animator>( inId );
    return animator ? animator->GetNormalizedTime( Text( inLayer ) ) : 0.f;
}


static void Eng_Animator_SetLayerWeight( EntityID inId, const uint8_t* inLayer, float inWeight )
{
    if( Animator* animator = ComponentOf<Animator>( inId ) ) animator->SetLayerWeight( Text( inLayer ), inWeight );
}


static bool Eng_Physics2D_Raycast( Vector2* inOrigin, Vector2* inDirection, float inMaxDistance, int inMask, ScriptRaycastHit* outHit )
{
    *outHit = ScriptRaycastHit();
    Physics2DCore* physics = GetEngine().Physics2D;
    RaycastHit hit;
    if( !physics || !physics->Raycast( *inOrigin, *inDirection, inMaxDistance, hit, Mask( inMask ) ) )
    {
        return false;
    }
    ToScript( hit, *outHit );
    return true;
}


static int Eng_Physics2D_OverlapCircle( Vector2* inCenter, float inRadius, int inMask, EntityID* outEntities, int inMax )
{
    std::vector<EntityID> found;
    if( Physics2DCore* physics = GetEngine().Physics2D )
    {
        for( const EntityHandle& entity : physics->OverlapCircle( *inCenter, inRadius, Mask( inMask ) ) )
        {
            found.push_back( entity ? entity.GetID() : EntityID{} );
        }
    }
    return Fill( found, outEntities, inMax );
}


static int Eng_Physics2D_OverlapPoint( Vector2* inPoint, int inMask, EntityID* outEntities, int inMax )
{
    std::vector<EntityID> found;
    if( Physics2DCore* physics = GetEngine().Physics2D )
    {
        for( const EntityHandle& entity : physics->OverlapPoint( *inPoint, Mask( inMask ) ) )
        {
            found.push_back( entity ? entity.GetID() : EntityID{} );
        }
    }
    return Fill( found, outEntities, inMax );
}


static void Eng_Rigidbody2D_AddForce( EntityID inId, Vector2* inForce, int inMode )
{
    if( Rigidbody2D* body = ComponentOf<Rigidbody2D>( inId ) ) body->AddForce( *inForce, static_cast<ForceMode>( std::clamp( inMode, 0, 3 ) ) );
}


static void Eng_Rigidbody2D_GetVelocity( EntityID inId, Vector2* outVelocity )
{
    Rigidbody2D* body = ComponentOf<Rigidbody2D>( inId );
    *outVelocity = body ? body->GetVelocity() : Vector2();
}


static void Eng_Rigidbody2D_SetVelocity( EntityID inId, Vector2* inVelocity )
{
    if( Rigidbody2D* body = ComponentOf<Rigidbody2D>( inId ) ) body->SetVelocity( *inVelocity );
}


void Register_QueryBindings( ScriptEngineAPI& inAPI )
{
    inAPI.Physics2D_Raycast = Eng_Physics2D_Raycast;
    inAPI.Physics2D_OverlapCircle = Eng_Physics2D_OverlapCircle;
    inAPI.Physics2D_OverlapPoint = Eng_Physics2D_OverlapPoint;
    inAPI.Rigidbody2D_AddForce = Eng_Rigidbody2D_AddForce;
    inAPI.Rigidbody2D_GetVelocity = Eng_Rigidbody2D_GetVelocity;
    inAPI.Rigidbody2D_SetVelocity = Eng_Rigidbody2D_SetVelocity;
    inAPI.Animator_Play = Eng_Animator_Play;
    inAPI.Animator_Stop = Eng_Animator_Stop;
    inAPI.Animator_IsPlaying = Eng_Animator_IsPlaying;
    inAPI.Animator_SetFloat = Eng_Animator_SetFloat;
    inAPI.Animator_GetFloat = Eng_Animator_GetFloat;
    inAPI.Animator_SetBool = Eng_Animator_SetBool;
    inAPI.Animator_GetBool = Eng_Animator_GetBool;
    inAPI.Animator_SetTrigger = Eng_Animator_SetTrigger;
    inAPI.Animator_GetState = Eng_Animator_GetState;
    inAPI.Animator_GetNormalizedTime = Eng_Animator_GetNormalizedTime;
    inAPI.Animator_SetLayerWeight = Eng_Animator_SetLayerWeight;
    inAPI.Physics_RaycastMasked = Eng_Physics_RaycastMasked;
    inAPI.Physics_RaycastAll = Eng_Physics_RaycastAll;
    inAPI.Physics_SphereCast = Eng_Physics_SphereCast;
    inAPI.Physics_OverlapBox = Eng_Physics_OverlapBox;
    inAPI.AudioSource_Play = Eng_AudioSource_Play;
    inAPI.AudioSource_Stop = Eng_AudioSource_Stop;
    inAPI.AudioSource_Pause = Eng_AudioSource_Pause;
    inAPI.AudioSource_IsPlaying = Eng_AudioSource_IsPlaying;
    inAPI.Particles_Play = Eng_Particles_Play;
    inAPI.Particles_Stop = Eng_Particles_Stop;
    inAPI.Particles_Burst = Eng_Particles_Burst;
    inAPI.Particles_GetCount = Eng_Particles_GetCount;
    inAPI.World_FindAllWithComponent = Eng_World_FindAllWithComponent;
    inAPI.World_FindAllByName = Eng_World_FindAllByName;
    inAPI.Transform_GetChildren = Eng_Transform_GetChildren;
    inAPI.Entity_GetGuid = Eng_Entity_GetGuid;
    inAPI.Entity_GetLayer = Eng_Entity_GetLayer;
    inAPI.Entity_SetLayer = Eng_Entity_SetLayer;
    inAPI.Input_Rumble = Eng_Input_Rumble;
    inAPI.App_Quit = Eng_App_Quit;
    inAPI.Window_GetSize = Eng_Window_GetSize;
    inAPI.Window_GetMode = Eng_Window_GetMode;
    inAPI.Window_SetMode = Eng_Window_SetMode;
}

#endif
