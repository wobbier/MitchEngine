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
#include <cmath>
#include <cstring>

#pragma region Class

Input::Input()
{
    KeyboardState = SDL_GetKeyboardState( nullptr );
    MouseState = SDL_GetMouseState( nullptr, nullptr );// use these params

    Listen<MouseScrollEvent>( [this]( const MouseScrollEvent& InEvent ) {
        if( CaptureInput )
        {
            MouseScroll = MouseScroll + InEvent.Scroll;
        }
    } );
    Listen<KeyPressEvent>( [this]( const KeyPressEvent& InEvent ) {
        if( CaptureInput )
        {
            LastKeyPressed = (KeyCode)InEvent.Key;
            m_keyEventsThisFrame.push_back( InEvent );
        }
    } );

    PreviousKeyboardState = static_cast<const uint8_t*>( calloc( SDL_NUM_SCANCODES, sizeof( uint8_t ) ) );
    PostUpdate();
}

void Input::Pause()
{
    CaptureInput = false;
    UpdateCursor();
}

void Input::Resume()
{
    CaptureInput = true;
    UpdateCursor();
}

void Input::Stop()
{
    RelativeMousePosition = Vector2();
    CaptureInput = false;
    UpdateCursor();
}

void Input::Update()
{
    if( CaptureInput )
    {
        int mouseX = 0;
        int mouseY = 0;
        MouseState = SDL_GetMouseState( &mouseX, &mouseY ) | m_simulatedMouseButtons;
        MousePosition = Vector2( mouseX, mouseY );
        int relativeMouse[2] = { 0, 0 };
        SDL_GetRelativeMouseState( &relativeMouse[0], &relativeMouse[1] );
        RelativeMousePosition = Vector2( relativeMouse[0], relativeMouse[1] );

        // Captured look always uses SDL's relative motion, including an embedded Game View.
        // Absolute position + a per-frame warp loses motion at window edges and can include
        // asynchronous warp events in the next frame's delta.
        const bool inRegionWindow = m_cursorWindow && SDL_GetMouseFocus() == m_cursorWindow && SDL_GetKeyboardFocus() == m_cursorWindow;
        // Relative mode can hide the pointer or report a different hover window. Keyboard
        // focus anywhere in this application's windows is sufficient while gameplay owns input.
        if( m_cursorWindow && WantsToCaptureMouse && !SDL_GetKeyboardFocus() )
        {
            RelativeMousePosition = Vector2();
        }
        else if( inRegionWindow && !WantsToCaptureMouse )
        {
            const int x = std::clamp( mouseX, m_cursorRect.x, m_cursorRect.x + m_cursorRect.w - 1 );
            const int y = std::clamp( mouseY, m_cursorRect.y, m_cursorRect.y + m_cursorRect.h - 1 );
            if( x != mouseX || y != mouseY )
            {
                SDL_WarpMouseInWindow( m_cursorWindow, x, y );
                MousePosition = Vector2( x, y );
            }
        }
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
    for( size_t key = 0; key < m_simulatedKeys.size(); ++key )
    {
        m_devices.Keys[key] |= m_simulatedKeys[key];
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

    // Carry this frame's edges to the next fixed step (BeginFixedStep).
    if( CaptureInput )
    {
        for( size_t key = 0; key < static_cast<size_t>( kKeyCount ); ++key )
        {
            if( KeyboardState[key] && !PreviousKeyboardState[key] )
            {
                m_pendingEdges.KeysPressed.set( key );
            }
            else if( !KeyboardState[key] && PreviousKeyboardState[key] )
            {
                m_pendingEdges.KeysReleased.set( key );
            }
        }
        m_pendingEdges.MousePressed |= MouseState & ~PreviousMouseState;
    }
    for( int slot = 0; slot < kMaxGamepads; ++slot )
    {
        const auto& buttons = m_devices.Gamepads[slot].Buttons;
        const auto& previous = m_previousDevices.Gamepads[slot].Buttons;
        for( size_t button = 0; button < buttons.size() && button < 32; ++button )
        {
            if( buttons[button] && !previous[button] )
            {
                m_pendingEdges.PadsPressed[slot] |= 1u << button;
            }
            else if( !buttons[button] && previous[button] )
            {
                m_pendingEdges.PadsReleased[slot] |= 1u << button;
            }
        }
    }
}


void Input::BeginFixedStep()
{
    m_stepEdges = m_pendingEdges;
    m_pendingEdges = Edges();
    m_inFixedStep = true;
    for( int player = 0; player < m_players.GetPlayerCount(); ++player )
    {
        m_players.GetActions( player ).BeginFixedStep();
    }
}


void Input::EndFixedStep()
{
    m_stepEdges = Edges();
    m_inFixedStep = false;
    for( int player = 0; player < m_players.GetPlayerCount(); ++player )
    {
        m_players.GetActions( player ).EndFixedStep();
    }
}


void Input::DiscardFixedStepEdges()
{
    m_pendingEdges = Edges();
    for( int player = 0; player < m_players.GetPlayerCount(); ++player )
    {
        m_players.GetActions( player ).DiscardFixedStepEdges();
    }
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

void Input::SetSimulatedKey( KeyCode InKey, bool InDown )
{
    if( static_cast<size_t>( InKey ) < m_simulatedKeys.size() )
    {
        m_simulatedKeys[static_cast<size_t>( InKey )] = InDown ? 1 : 0;
    }
}

void Input::ClearSimulatedKeys()
{
    m_simulatedKeys.fill( 0 );
}

#pragma endregion

#pragma region KeyboardInput

bool Input::IsKeyDown( KeyCode key )
{
    return CaptureInput && KeyboardState[(uint32_t)key];
}

bool Input::WasKeyPressed( KeyCode key )
{
    if( m_inFixedStep )
    {
        return CaptureInput && (uint32_t)key < static_cast<uint32_t>( kKeyCount ) && m_stepEdges.KeysPressed.test( (uint32_t)key );
    }
    return CaptureInput && !PreviousKeyboardState[(uint32_t)key] && KeyboardState[(uint32_t)key];
}

bool Input::WasKeyReleased( KeyCode key )
{
    if( m_inFixedStep )
    {
        return CaptureInput && (uint32_t)key < static_cast<uint32_t>( kKeyCount ) && m_stepEdges.KeysReleased.test( (uint32_t)key );
    }
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
    if( m_inFixedStep )
    {
        return InSlot >= 0 && InSlot < kMaxGamepads && ( m_stepEdges.PadsPressed[InSlot] >> static_cast<uint32_t>( InButton ) & 1u );
    }
    return IsGamepadButtonDown( InButton, InSlot ) && !m_previousDevices.Gamepads[InSlot].Buttons[static_cast<size_t>( InButton )];
}


bool Input::WasGamepadButtonReleased( GamepadButton InButton, int InSlot ) const
{
    if( m_inFixedStep )
    {
        return InSlot >= 0 && InSlot < kMaxGamepads && ( m_stepEdges.PadsReleased[InSlot] >> static_cast<uint32_t>( InButton ) & 1u );
    }
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
    WantsToCaptureMouse = Capture;
    UpdateCursor();
}


void Input::SetCursorRegion( SDL_Window* InWindow, const Vector2& InMin, const Vector2& InSize )
{
    const SDL_Rect rect{ static_cast<int>( std::ceil( InMin.x ) ), static_cast<int>( std::ceil( InMin.y ) ),
                         std::max( static_cast<int>( InSize.x ), 1 ), std::max( static_cast<int>( InSize.y ), 1 ) };
    if( InWindow != m_cursorWindow && m_cursorWindow && m_cursorConfined )
    {
        SDL_SetWindowGrab( m_cursorWindow, SDL_FALSE );
        m_cursorConfined = false;
    }
    m_cursorWindow = InWindow;
    m_cursorRect = rect;
    UpdateCursor();
}


void Input::ClearCursorRegion()
{
    if( m_cursorWindow && m_cursorConfined )
    {
        SDL_SetWindowGrab( m_cursorWindow, SDL_FALSE );
    }
    m_cursorConfined = false;
    m_cursorWindow = nullptr;
    UpdateCursor();
}


void Input::UpdateCursor()
{
    // Only the input that is capturing holds the cursor: the editor and the game each own an
    // Input, and stopping the idle one must not undo the other's state.
    const bool lock = CaptureInput && WantsToCaptureMouse;
    const bool relative = lock;
    if( relative != m_relativeMode )
    {
        // SDL binds relative capture to the keyboard-focused window. Play may begin from
        // an inspector or toolbar in a different platform window than the Game View.
        if( relative && m_cursorWindow && SDL_GetKeyboardFocus() != m_cursorWindow )
        {
            SDL_SetWindowInputFocus( m_cursorWindow );
        }
#if USING( ME_PLATFORM_LINUX )
        // Hyprland/XWayland's XInput raw stream can include reverse motion that cancels
        // captured look. SDL's warp-relative implementation filters its own recenter events.
        // Keep this in SDL rather than deriving deltas from absolute positions each frame.
        const char* driver = SDL_GetCurrentVideoDriver();
        const char* hyprland = SDL_getenv( "HYPRLAND_INSTANCE_SIGNATURE" );
        if( relative && driver && SDL_strcmp( driver, "x11" ) == 0 && hyprland && *hyprland )
        {
            SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1" );
        }
#endif
        SDL_SetRelativeMouseMode( relative ? SDL_TRUE : SDL_FALSE );
        m_relativeMode = SDL_GetRelativeMouseMode() == SDL_TRUE;
    }

    // Relative mode owns captured motion; an uncaptured cursor stays inside the view region.
    // (SDL_SetWindowMouseRect would be tighter, but its X11 pointer barriers can fail fatally.)
    const bool confine = CaptureInput && m_cursorWindow;
    if( confine != m_cursorConfined && m_cursorWindow )
    {
        SDL_SetWindowGrab( m_cursorWindow, confine ? SDL_TRUE : SDL_FALSE );
    }
    m_cursorConfined = confine;

    // Relative mode hides the captured cursor, including inside a Game View.
    const bool hide = lock && m_cursorWindow;
    if( hide != m_cursorHidden )
    {
        SDL_ShowCursor( hide ? SDL_DISABLE : SDL_ENABLE );
        m_cursorHidden = hide;
    }
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
    if( m_inFixedStep )
    {
        return ( m_stepEdges.MousePressed & SDL_BUTTON( (uint32_t)mouseButton ) ) != 0;
    }
    return IsMouseButtonDown( mouseButton ) && !( PreviousMouseState & SDL_BUTTON( (uint32_t)mouseButton ) );
}

#pragma endregion
