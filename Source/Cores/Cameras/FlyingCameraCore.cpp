#include "PCH.h"
#include "FlyingCameraCore.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Engine/Input.h"
#include "Engine/Engine.h"
#include <algorithm>

FlyingCameraCore::FlyingCameraCore()
    : Base( ComponentFilter().Requires<FlyingCamera>().Requires<Camera>().Requires<Transform>() )
{
}


void FlyingCameraCore::Update( const UpdateContext& InContext )
{
    OPTICK_CATEGORY( "FlyingCameraCore::Update", Optick::Category::Camera );
    Input& input = GetEngine().GetInput();
    const float dt = InContext.GetUnscaledDeltaTime();
    const bool hasMove = input.GetActions().HasAction( "Move" );
    const bool hasLook = input.GetActions().HasAction( "Look" );

    for( Entity& entity : GetEntities() )
    {
        FlyingCamera& flying = entity.GetComponent<FlyingCamera>();
        Transform& transform = entity.GetComponent<Transform>();

        // Movement in the camera's frame.
        Vector2 move = hasMove ? input.GetAction( "Move" ).GetVector2() : Vector2();
        if( !hasMove )
        {
            move.x = ( input.IsKeyDown( KeyCode::D ) ? 1.f : 0.f ) - ( input.IsKeyDown( KeyCode::A ) ? 1.f : 0.f );
            move.y = ( input.IsKeyDown( KeyCode::W ) ? 1.f : 0.f ) - ( input.IsKeyDown( KeyCode::S ) ? 1.f : 0.f );
        }
        float rise = ( input.IsKeyDown( KeyCode::E ) ? 1.f : 0.f ) - ( input.IsKeyDown( KeyCode::Q ) ? 1.f : 0.f );
        rise += ( input.IsGamepadButtonDown( GamepadButton::RightShoulder ) ? 1.f : 0.f ) - ( input.IsGamepadButtonDown( GamepadButton::LeftShoulder ) ? 1.f : 0.f );
        const bool sprint = input.IsKeyDown( KeyCode::LeftShift ) || input.IsGamepadButtonDown( GamepadButton::LeftStickPress );
        const float speed = flying.FlyingSpeed * ( sprint ? std::max( flying.SpeedModifier, 1.f ) : 1.f ) * dt;
        const Vector3 front = transform.Front();
        const Vector3 right = transform.Right();
        transform.Translate( ( front * move.y + right * move.x + Vector3::Up * std::clamp( rise, -1.f, 1.f ) ) * speed );

        // Look: mouse while the right button is held, the gamepad stick any time.
        Vector2 look;
        if( input.IsMouseButtonDown( MouseButton::Right ) )
        {
            const Vector2 mouse = input.GetRelativeMousePosition();
            look = Vector2( mouse.x, mouse.y ) * flying.LookSensitivity;
        }
        if( hasLook )
        {
            const Vector2 stick = input.GetGamepadStick( GamepadStick::Right );
            look = look + Vector2( stick.x, -stick.y ) * ( 120.f * dt );
        }
        if( look.x != 0.f || look.y != 0.f )
        {
            Vector3 euler = transform.GetRotationEuler();
            euler.x = std::clamp( euler.x + look.y, -89.f, 89.f );
            euler.y += look.x;
            euler.z = 0.f;
            transform.SetRotation( euler );
        }
    }
}
