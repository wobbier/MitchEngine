#include "PCH.h"
#include "Query.bindings.h"
#include "Components/Physics/CharacterController2D.h"

#if USING( ME_SCRIPTING )

#include "Components/Animation/Animator.h"
#include "Components/Audio/AudioSource.h"
#include "Components/Camera.h"
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include "Components/Effects/ParticleSystem.h"
#include "Components/Graphics/SpriteRenderer.h"
#include "Components/Graphics/Tilemap.h"
#include "Components/Graphics/TextRenderer.h"
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
// scripts, 3D and 2D, and camera projection (CS-5 areas a, b, c, e, f, g, h and j).

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
    PhysicsCore* physics = ScriptBindings::Physics();
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
    PhysicsCore* physics = ScriptBindings::Physics();
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
    PhysicsCore* physics = ScriptBindings::Physics();
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
    PhysicsCore* physics = ScriptBindings::Physics();
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
    Physics2DCore* physics = ScriptBindings::Physics2D();
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
    if( Physics2DCore* physics = ScriptBindings::Physics2D() )
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
    if( Physics2DCore* physics = ScriptBindings::Physics2D() )
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


static void Eng_Character2D_SetMoveInput( EntityID inId, float inHorizontal )
{
    if( CharacterController2D* character = ComponentOf<CharacterController2D>( inId ) ) character->SetMoveInput( inHorizontal );
}


static void Eng_Character2D_Jump( EntityID inId )
{
    if( CharacterController2D* character = ComponentOf<CharacterController2D>( inId ) ) character->Jump();
}


static bool Eng_Character2D_IsGrounded( EntityID inId )
{
    CharacterController2D* character = ComponentOf<CharacterController2D>( inId );
    return character && character->IsOnGround();
}


static void Eng_Character2D_GetVelocity( EntityID inId, Vector2* outVelocity )
{
    CharacterController2D* character = ComponentOf<CharacterController2D>( inId );
    *outVelocity = character ? character->GetVelocity() : Vector2();
}


static float Eng_Character2D_GetMaxSpeed( EntityID inId )
{
    CharacterController2D* character = ComponentOf<CharacterController2D>( inId );
    return character ? character->MaxSpeed : 0.f;
}


static void Eng_Character2D_SetMaxSpeed( EntityID inId, float inSpeed )
{
    if( CharacterController2D* character = ComponentOf<CharacterController2D>( inId ) ) character->MaxSpeed = std::max( inSpeed, 0.f );
}


static void Eng_Character2D_Teleport( EntityID inId, Vector2* inPosition )
{
    if( CharacterController2D* character = ComponentOf<CharacterController2D>( inId ) ) character->Teleport( *inPosition );
}


namespace
{
    // World to clip space: projection * view (bx matrices, read as glm's column-major).
    bool ViewProjection( EntityID InCamera, glm::mat4& OutMatrix, Vector2& OutSize )
    {
        Camera* camera = ComponentOf<Camera>( InCamera );
        if( !camera )
        {
            return false;
        }
        OutSize = camera->GetViewportSize();
        OutMatrix = camera->GetProjectionMatrix().GetInternalMatrix() * camera->WorldToCamera.GetInternalMatrix();
        return OutSize.x > 0.f && OutSize.y > 0.f;
    }
}


static void Eng_Camera_ScreenToRay( EntityID inCamera, Vector2* inScreen, Vector3* outOrigin, Vector3* outDirection )
{
    *outOrigin = Vector3();
    *outDirection = Vector3( 0.f, 0.f, 1.f );
    glm::mat4 viewProjection;
    Vector2 size;
    if( !ViewProjection( inCamera, viewProjection, size ) )
    {
        return;
    }
    const glm::mat4 inverse = glm::inverse( viewProjection );
    const float x = inScreen->x / size.x * 2.f - 1.f;
    const float y = 1.f - inScreen->y / size.y * 2.f;
    const float nearDepth = bgfx::getCaps()->homogeneousDepth ? -1.f : 0.f;
    glm::vec4 nearPoint = inverse * glm::vec4( x, y, nearDepth, 1.f );
    glm::vec4 farPoint = inverse * glm::vec4( x, y, 1.f, 1.f );
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const glm::vec3 direction = glm::normalize( glm::vec3( farPoint ) - glm::vec3( nearPoint ) );
    *outOrigin = Vector3( nearPoint.x, nearPoint.y, nearPoint.z );
    *outDirection = Vector3( direction.x, direction.y, direction.z );
}


static void Eng_Camera_WorldToScreen( EntityID inCamera, const Vector3* inWorld, Vector3* outScreen )
{
    *outScreen = Vector3();
    glm::mat4 viewProjection;
    Vector2 size;
    if( !ViewProjection( inCamera, viewProjection, size ) )
    {
        return;
    }
    const glm::vec4 clip = viewProjection * glm::vec4( inWorld->x, inWorld->y, inWorld->z, 1.f );
    if( std::abs( clip.w ) < 1e-6f )
    {
        return;
    }
    const float x = clip.x / clip.w;
    const float y = clip.y / clip.w;
    *outScreen = Vector3( ( x + 1.f ) * 0.5f * size.x, ( 1.f - y ) * 0.5f * size.y, clip.w );
}


static void Eng_Camera_GetOutputSize( EntityID inCamera, Vector2* outSize )
{
    Camera* camera = ComponentOf<Camera>( inCamera );
    *outSize = camera ? camera->GetViewportSize() : Vector2();
}


// The mouse in the game's pixels (in the editor: relative to the game view).
static void Eng_Input_GetMousePosition( Vector2* outPosition )
{
    Input& input = GetEngine().GetInput();
    *outPosition = input.GetMousePosition() - input.GetMouseOffset();
}


static void Eng_Sprite_SetSprite( EntityID inId, const uint8_t* inSheet, const uint8_t* inSprite )
{
    if( SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId ) )
    {
        const std::string sheet = Text( inSheet );
        if( !sheet.empty() && sprite->Sheet.GetLocalPathString() != sheet )
        {
            sprite->Sheet = Path( sheet );
        }
        sprite->SpriteName = Text( inSprite );
    }
}


static int Eng_Sprite_GetSprite( EntityID inId, uint8_t* outSprite, int inSize )
{
    SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId );
    const std::string name = sprite ? sprite->SpriteName : std::string();
    if( outSprite && inSize > 0 )
    {
        const int length = std::min( static_cast<int>( name.size() ), inSize - 1 );
        std::memcpy( outSprite, name.data(), static_cast<size_t>( length ) );
        outSprite[length] = 0;
    }
    return static_cast<int>( name.size() );
}


static void Eng_Sprite_SetTexture( EntityID inId, const uint8_t* inTexture )
{
    if( SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId ) )
    {
        sprite->Texture = Path( Text( inTexture ) );
    }
}


static void Eng_Sprite_SetFlip( EntityID inId, bool inFlipX, bool inFlipY )
{
    if( SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId ) )
    {
        sprite->FlipX = inFlipX;
        sprite->FlipY = inFlipY;
    }
}


static int Eng_Sprite_GetFlip( EntityID inId )
{
    SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId );
    return sprite ? ( sprite->FlipX ? 1 : 0 ) | ( sprite->FlipY ? 2 : 0 ) : 0;
}


static void Eng_Sprite_SetColor( EntityID inId, Vector4* inColor )
{
    if( SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId ); sprite && inColor )
    {
        sprite->Tint = Vector3( inColor->x, inColor->y, inColor->z );
        sprite->Opacity = inColor->w;
    }
}


static void Eng_Sprite_GetColor( EntityID inId, Vector4* outColor )
{
    SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId );
    if( outColor )
    {
        *outColor = sprite ? Vector4( sprite->Tint.x, sprite->Tint.y, sprite->Tint.z, sprite->Opacity ) : Vector4( 1.f, 1.f, 1.f, 1.f );
    }
}


static void Eng_Sprite_SetSorting( EntityID inId, int inLayer, int inOrder )
{
    if( SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId ) )
    {
        sprite->SortingLayer = inLayer;
        sprite->OrderInLayer = inOrder;
    }
}


static int Eng_Sprite_GetSortingLayer( EntityID inId )
{
    SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId );
    return sprite ? sprite->SortingLayer : 0;
}


static int Eng_Sprite_GetOrderInLayer( EntityID inId )
{
    SpriteRenderer* sprite = ComponentOf<SpriteRenderer>( inId );
    return sprite ? sprite->OrderInLayer : 0;
}


static void Eng_Tilemap_SetTile( EntityID inId, int inX, int inY, int inIndex )
{
    if( Tilemap* tilemap = ComponentOf<Tilemap>( inId ) ) tilemap->SetTile( inX, inY, inIndex );
}


static int Eng_Tilemap_GetTile( EntityID inId, int inX, int inY )
{
    Tilemap* tilemap = ComponentOf<Tilemap>( inId );
    return tilemap ? tilemap->GetTile( inX, inY ) : -1;
}


static void Eng_Tilemap_FillRect( EntityID inId, int inX0, int inY0, int inX1, int inY1, int inIndex )
{
    if( Tilemap* tilemap = ComponentOf<Tilemap>( inId ) ) tilemap->FillRect( inX0, inY0, inX1, inY1, inIndex );
}


static void Eng_Tilemap_Clear( EntityID inId )
{
    if( Tilemap* tilemap = ComponentOf<Tilemap>( inId ) ) tilemap->Clear();
}


static int Eng_Tilemap_GetTileCount( EntityID inId )
{
    Tilemap* tilemap = ComponentOf<Tilemap>( inId );
    return tilemap ? static_cast<int>( tilemap->GetTileCount() ) : 0;
}


static void Eng_Tilemap_WorldToCell( EntityID inId, const Vector3* inWorld, int* outCell )
{
    Tilemap* tilemap = ComponentOf<Tilemap>( inId );
    Transform* transform = ComponentOf<Transform>( inId );
    if( !outCell || !inWorld )
    {
        return;
    }
    outCell[0] = outCell[1] = 0;
    if( tilemap && transform )
    {
        const glm::vec4 local = transform->GetWorldToLocalMatrix().GetInternalMatrix() * glm::vec4( inWorld->InternalVector, 1.f );
        tilemap->LocalToCell( Vector2( local.x, local.y ), outCell[0], outCell[1] );
    }
}


static void Eng_Text_SetText( EntityID inId, const uint8_t* inText )
{
    if( TextRenderer* text = ComponentOf<TextRenderer>( inId ) ) text->Text = Text( inText );
}


static int Eng_Text_GetText( EntityID inId, uint8_t* outText, int inSize )
{
    TextRenderer* text = ComponentOf<TextRenderer>( inId );
    const std::string value = text ? text->Text : std::string();
    if( outText && inSize > 0 )
    {
        const int length = std::min( static_cast<int>( value.size() ), inSize - 1 );
        std::memcpy( outText, value.data(), static_cast<size_t>( length ) );
        outText[length] = 0;
    }
    return static_cast<int>( value.size() );
}


static void Eng_Text_SetColor( EntityID inId, Vector4* inColor )
{
    if( TextRenderer* text = ComponentOf<TextRenderer>( inId ); text && inColor )
    {
        text->Color = Vector3( inColor->x, inColor->y, inColor->z );
        text->Opacity = inColor->w;
    }
}


void Register_QueryBindings( ScriptEngineAPI& inAPI )
{
    inAPI.Camera_ScreenToRay = Eng_Camera_ScreenToRay;
    inAPI.Camera_WorldToScreen = Eng_Camera_WorldToScreen;
    inAPI.Camera_GetOutputSize = Eng_Camera_GetOutputSize;
    inAPI.Input_GetMousePosition = Eng_Input_GetMousePosition;
    inAPI.Physics2D_Raycast = Eng_Physics2D_Raycast;
    inAPI.Physics2D_OverlapCircle = Eng_Physics2D_OverlapCircle;
    inAPI.Physics2D_OverlapPoint = Eng_Physics2D_OverlapPoint;
    inAPI.Rigidbody2D_AddForce = Eng_Rigidbody2D_AddForce;
    inAPI.Rigidbody2D_GetVelocity = Eng_Rigidbody2D_GetVelocity;
    inAPI.Rigidbody2D_SetVelocity = Eng_Rigidbody2D_SetVelocity;
    inAPI.Character2D_SetMoveInput = Eng_Character2D_SetMoveInput;
    inAPI.Character2D_Jump = Eng_Character2D_Jump;
    inAPI.Character2D_IsGrounded = Eng_Character2D_IsGrounded;
    inAPI.Character2D_GetVelocity = Eng_Character2D_GetVelocity;
    inAPI.Character2D_GetMaxSpeed = Eng_Character2D_GetMaxSpeed;
    inAPI.Character2D_SetMaxSpeed = Eng_Character2D_SetMaxSpeed;
    inAPI.Character2D_Teleport = Eng_Character2D_Teleport;
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
    inAPI.Sprite_SetSprite = Eng_Sprite_SetSprite;
    inAPI.Sprite_GetSprite = Eng_Sprite_GetSprite;
    inAPI.Sprite_SetTexture = Eng_Sprite_SetTexture;
    inAPI.Sprite_SetFlip = Eng_Sprite_SetFlip;
    inAPI.Sprite_GetFlip = Eng_Sprite_GetFlip;
    inAPI.Sprite_SetColor = Eng_Sprite_SetColor;
    inAPI.Sprite_GetColor = Eng_Sprite_GetColor;
    inAPI.Sprite_SetSorting = Eng_Sprite_SetSorting;
    inAPI.Sprite_GetSortingLayer = Eng_Sprite_GetSortingLayer;
    inAPI.Sprite_GetOrderInLayer = Eng_Sprite_GetOrderInLayer;
    inAPI.Tilemap_SetTile = Eng_Tilemap_SetTile;
    inAPI.Tilemap_GetTile = Eng_Tilemap_GetTile;
    inAPI.Tilemap_FillRect = Eng_Tilemap_FillRect;
    inAPI.Tilemap_Clear = Eng_Tilemap_Clear;
    inAPI.Tilemap_GetTileCount = Eng_Tilemap_GetTileCount;
    inAPI.Tilemap_WorldToCell = Eng_Tilemap_WorldToCell;
    inAPI.Text_SetText = Eng_Text_SetText;
    inAPI.Text_GetText = Eng_Text_GetText;
    inAPI.Text_SetColor = Eng_Text_SetColor;
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
