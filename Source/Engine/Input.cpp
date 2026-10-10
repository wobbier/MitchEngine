#include "PCH.h"

#if USING( ME_PLATFORM_WINDOWS )
#include <WinUser.h>
#endif

#include "Engine/Input.h"
#include "CLog.h"
#include <string>
#include <iostream>
#include "SDL.h"
#include "Engine.h"
#include <Window/IWindow.h>
#include <Window/SDLWindow.h>
#include "Input/Gamepads.h"
#include "Input/InputActionsAsset.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <cstring>

#pragma region Class

Input::Input()
{
    KeyboardState = SDL_GetKeyboardState( nullptr );
    MouseState = SDL_GetMouseState( nullptr, nullptr );// use these params

    std::vector<TypeId> events;
    events.push_back( MouseScrollEvent::GetEventId() );
    events.push_back( KeyPressEvent::GetEventId() );
    EventManager::GetInstance().RegisterReceiver( this, events );

    PreviousKeyboardState = static_cast<const uint8_t*>( malloc( sizeof( uint8_t ) * SDL_NUM_SCANCODES ) );
    PostUpdate();
}

bool Input::OnEvent( const BaseEvent& evt )
{
    if( CaptureInput )
    {
        if( evt.GetEventId() == MouseScrollEvent::GetEventId() )
        {
            if( CaptureInput )
            {
                const MouseScrollEvent& event = static_cast<const MouseScrollEvent&>( evt );
                MouseScroll = MouseScroll + event.Scroll;
            }
        }
        if( evt.GetEventId() == KeyPressEvent::GetEventId() )
        {
            if( CaptureInput )
            {
                const KeyPressEvent& event = static_cast<const KeyPressEvent&>( evt );
                LastKeyPressed = (KeyCode)event.Key;

                //BRUH_FMT( "KeyEvent: Key %i, State %i", event.Key, event.State );
                m_keyEventsThisFrame.push_back( event );
            }
        }
    }

    return false;
}

void Input::Pause()
{
    CaptureInput = false;
}

void Input::Resume()
{
    CaptureInput = true;
    SetMouseCapture( WantsToCaptureMouse );
}

void Input::Stop()
{
    RelativeMousePosition = Vector2();
    SDL_SetRelativeMouseMode( SDL_FALSE );
    SDL_ShowCursor( SDL_ENABLE );
    CaptureInput = false;
}

void Input::Update()
{
    if( CaptureInput )
    {
        int mouseX = 0;
        int mouseY = 0;
        MouseState = SDL_GetMouseState( &mouseX, &mouseY );
        MousePosition = Vector2( mouseX, mouseY );
        int relativeMouse[2] = { 0, 0 };
        SDL_GetRelativeMouseState( &relativeMouse[0], &relativeMouse[1] );
        RelativeMousePosition = Vector2( relativeMouse[0], relativeMouse[1] );
    }

    // Device snapshot for actions and the gamepad API (nothing while another context has input).
    m_previousDevices = m_devices;
    m_devices = InputDeviceState();
    if( CaptureInput )
    {
        int keyCount = 0;
        const uint8_t* keys = SDL_GetKeyboardState( &keyCount );
        std::memcpy( m_devices.Keys.data(), keys, std::min<size_t>( static_cast<size_t>( keyCount ), m_devices.Keys.size() ) );
        m_devices.MouseButtons = MouseState;
        m_devices.MouseDelta = Vector2( RelativeMousePosition.x, -RelativeMousePosition.y );
        m_devices.Scroll = MouseScroll - PreviousMouseScroll;
        Gamepads::Get().Snapshot( m_devices.Gamepads );
    }

    if( m_actionAsset && m_actionAsset->Version != m_actionVersion )
    {
        m_actionVersion = m_actionAsset->Version;
        m_players.SetMap( m_actionAsset->Map );
    }
    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = m_hasUpdated ? std::min( std::chrono::duration<float>( now - m_lastUpdate ).count(), 0.25f ) : 0.f;
    m_lastUpdate = now;
    m_hasUpdated = true;
    m_players.Update( m_devices, m_previousDevices, deltaSeconds );
}

void Input::PostUpdate()
{
    if( CaptureInput )
    {
        PreviousKeyboardState = static_cast<const uint8_t*>( memcpy( (void*)PreviousKeyboardState, (void*)KeyboardState, sizeof( uint8_t ) * SDL_NUM_SCANCODES ) );
    }
    else
    {
        PreviousKeyboardState = static_cast<const uint8_t*>( memset( (void*)PreviousKeyboardState, 0, sizeof( uint8_t ) * SDL_NUM_SCANCODES ) );
    }
    PreviousMouseState = MouseState;
    PreviousMouseScroll = MouseScroll;
    m_keyEventsThisFrame.clear();
}

#pragma endregion

#pragma region KeyboardInput

bool Input::IsKeyDown( KeyCode key )
{
    return CaptureInput && KeyboardState[(uint32_t)key];
}

bool Input::WasKeyPressed( KeyCode key )
{
    return CaptureInput && !PreviousKeyboardState[(uint32_t)key] && KeyboardState[(uint32_t)key];
}

bool Input::WasKeyReleased( KeyCode key )
{
    return CaptureInput && PreviousKeyboardState[(uint32_t)key] && !KeyboardState[(uint32_t)key];
}

KeyState Input::GetKeyCodeState( KeyCode key )
{
    if( !CaptureInput )
        return KeyState::None;

    if( WasKeyPressed( key ) )
        return KeyState::Pressed;

    if( PreviousKeyboardState[(uint32_t)key] && KeyboardState[(uint32_t)key] )
        return KeyState::Held;

    if( WasKeyReleased( key ) )
        return KeyState::Released;

    return KeyState::None;
}

#pragma endregion

#pragma region Actions

const InputActionState& Input::GetAction( const std::string& InName ) const
{
    return m_actions.GetAction( InName );
}


bool Input::LoadActions( const Path& InPath )
{
    if( !InPath.Exists )
    {
        return false;
    }
    m_actionAsset = ResourceCache::GetInstance().Get<InputActionsResource>( InPath );
    if( !m_actionAsset )
    {
        return false;
    }
    m_actionVersion = m_actionAsset->Version;
    m_players.SetMap( m_actionAsset->Map );
    return true;
}


const InputActionState& Input::GetAction( int InPlayer, const std::string& InName ) const
{
    return m_players.GetActions( InPlayer ).GetAction( InName );
}


bool Input::RumblePlayer( int InPlayer, float InLow, float InHigh, float InSeconds )
{
    const int slot = m_players.GetGamepad( InPlayer );
    return slot >= 0 && Rumble( InLow, InHigh, InSeconds, slot );
}

#pragma endregion

#pragma region Gamepads

int Input::GetGamepadCount() const
{
    return Gamepads::Get().GetConnectedCount();
}


bool Input::IsGamepadConnected( int InSlot ) const
{
    return Gamepads::Get().IsConnected( InSlot );
}


std::string Input::GetGamepadName( int InSlot ) const
{
    return Gamepads::Get().GetName( InSlot );
}


bool Input::IsGamepadButtonDown( GamepadButton InButton, int InSlot ) const
{
    return InSlot >= 0 && InSlot < kMaxGamepads && m_devices.Gamepads[InSlot].Buttons[static_cast<size_t>( InButton )];
}


bool Input::WasGamepadButtonPressed( GamepadButton InButton, int InSlot ) const
{
    return IsGamepadButtonDown( InButton, InSlot ) && !m_previousDevices.Gamepads[InSlot].Buttons[static_cast<size_t>( InButton )];
}


bool Input::WasGamepadButtonReleased( GamepadButton InButton, int InSlot ) const
{
    return InSlot >= 0 && InSlot < kMaxGamepads && !m_devices.Gamepads[InSlot].Buttons[static_cast<size_t>( InButton )] && m_previousDevices.Gamepads[InSlot].Buttons[static_cast<size_t>( InButton )];
}


float Input::GetGamepadAxis( GamepadAxis InAxis, int InSlot ) const
{
    if( InSlot < 0 || InSlot >= kMaxGamepads )
    {
        return 0.f;
    }
    const GamepadState& pad = m_devices.Gamepads[InSlot];
    switch( InAxis )
    {
    case GamepadAxis::LeftTrigger:
    case GamepadAxis::RightTrigger:
        return InputActionSystem::ApplyTriggerDeadzone( pad.Axes[static_cast<size_t>( InAxis )], m_actions.GetMap().TriggerDeadzone );
    case GamepadAxis::LeftX:
    case GamepadAxis::LeftY:
        return InAxis == GamepadAxis::LeftX ? GetGamepadStick( GamepadStick::Left, InSlot ).x : GetGamepadStick( GamepadStick::Left, InSlot ).y;
    case GamepadAxis::RightX:
    case GamepadAxis::RightY:
        return InAxis == GamepadAxis::RightX ? GetGamepadStick( GamepadStick::Right, InSlot ).x : GetGamepadStick( GamepadStick::Right, InSlot ).y;
    default:
        return 0.f;
    }
}


Vector2 Input::GetGamepadStick( GamepadStick InStick, int InSlot ) const
{
    if( InSlot < 0 || InSlot >= kMaxGamepads )
    {
        return Vector2();
    }
    const GamepadState& pad = m_devices.Gamepads[InSlot];
    const size_t x = InStick == GamepadStick::Left ? static_cast<size_t>( GamepadAxis::LeftX ) : static_cast<size_t>( GamepadAxis::RightX );
    const InputActionMap& map = m_actions.GetMap();
    return InputActionSystem::ApplyStickDeadzone( Vector2( pad.Axes[x], pad.Axes[x + 1] ), map.StickDeadzone, map.StickOuterDeadzone );
}


bool Input::Rumble( float InLow, float InHigh, float InSeconds, int InSlot )
{
    return CaptureInput && Gamepads::Get().Rumble( InSlot, InLow, InHigh, InSeconds );
}

#pragma endregion

#pragma region MouseInput

Vector2 Input::GetMousePosition() const
{
    return MousePosition;
}

Vector2 Input::GetGlobalMousePosition() const
{
    // TODO: Cache this :/
    int mouse_x_global, mouse_y_global;
    SDL_GetGlobalMouseState( &mouse_x_global, &mouse_y_global );
    return { mouse_x_global, mouse_y_global };
}

Vector2 Input::GetRelativeMousePosition() const
{
    return RelativeMousePosition;
}

void Input::SetMousePosition( const Vector2& InPosition )
{
    // this just needs to be redone generically, too lazy atm
    if( CaptureInput )
    {
#if USING( ME_EDITOR_WIN64 )
        if( !GameWindow )
        {
            GameWindow = GetEngine().GetWindow();
        }
        auto win = static_cast<SDLWindow*>( GameWindow )->WindowHandle;
        SDL_WarpMouseInWindow( win, GameWindow->GetSize().x / 2.f, GameWindow->GetSize().y / 2.f );
        //Vector2 pos = Offset + InPosition;
        //SetCursorPos(static_cast<int>(pos.x), static_cast<int>(pos.y));
#endif
    }
}

Vector2 Input::GetMouseOffset()
{
    return Offset;
}

Vector2 Input::GetMouseScrollOffset()
{
    return MouseScroll;
}

Vector2 Input::GetMouseScrollDelta()
{
    return MouseScroll - PreviousMouseScroll;
}

const char* Input::GetKeyCodeName( KeyCode inKey )
{
    return SDL_GetScancodeName( (SDL_Scancode)inKey );
}

KeyCode Input::GetLastKeyPressed()
{
    return LastKeyPressed;
}

void Input::SetMouseCapture( bool Capture )
{
    if( CaptureInput )
    {
        if( Capture )
        {
            SDL_SetRelativeMouseMode( SDL_TRUE );
            //SDL_CaptureMouse(SDL_TRUE);
            //SDL_ShowCursor(SDL_DISABLE);
        }
        else
        {
            SDL_SetRelativeMouseMode( SDL_FALSE );
            //SDL_CaptureMouse(SDL_FALSE);
            //SDL_ShowCursor(SDL_ENABLE);
        }
    }
    WantsToCaptureMouse = Capture;
}

void Input::SetMouseOffset( const Vector2& InOffset )
{
    Offset = InOffset;
}

bool Input::IsMouseButtonDown( MouseButton mouseButton )
{
    return MouseState & SDL_BUTTON( (uint32_t)mouseButton );
}

bool Input::WasMouseButtonPressed( MouseButton mouseButton )
{
    return IsMouseButtonDown( mouseButton ) && !( PreviousMouseState & SDL_BUTTON( (uint32_t)mouseButton ) );
}

#pragma endregion
