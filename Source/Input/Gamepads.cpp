#include "PCH.h"
#include "Gamepads.h"
#include "CLog.h"
#include <SDL.h>
#include <algorithm>

namespace
{
    Uint16 Motor( float InStrength )
    {
        return static_cast<Uint16>( std::clamp( InStrength, 0.f, 1.f ) * 65535.f );
    }


    Uint32 Duration( float InSeconds )
    {
        return static_cast<Uint32>( std::max( InSeconds, 0.f ) * 1000.f );
    }
}


Gamepads& Gamepads::Get()
{
    static Gamepads instance;
    return instance;
}


void Gamepads::OnDeviceAdded( int InDeviceIndex )
{
    if( !SDL_IsGameController( InDeviceIndex ) )
    {
        return;
    }
    const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID( InDeviceIndex );
    if( std::find( m_instances.begin(), m_instances.end(), instance ) != m_instances.end() )
    {
        return;   // already open (SDL reports pads present at startup as added too)
    }
    auto slot = std::find( m_pads.begin(), m_pads.end(), nullptr );
    if( slot == m_pads.end() )
    {
        BRUH( "Gamepad ignored: all " + std::to_string( kMaxGamepads ) + " slots are in use" );
        return;
    }
    SDL_GameController* pad = SDL_GameControllerOpen( InDeviceIndex );
    if( !pad )
    {
        BRUH( std::string( "Gamepad failed to open: " ) + SDL_GetError() );
        return;
    }
    const int index = static_cast<int>( slot - m_pads.begin() );
    m_pads[index] = pad;
    m_instances[index] = instance;

    GamepadConnectionEvent event;
    event.Slot = index;
    event.Connected = true;
    event.Name = GetName( index );
    CLog::Log( CLog::LogType::Info, "Gamepad " + std::to_string( index ) + " connected: " + event.Name );
    event.Fire();
}


void Gamepads::OnDeviceRemoved( int InInstanceId )
{
    for( int slot = 0; slot < kMaxGamepads; ++slot )
    {
        if( m_pads[slot] && m_instances[slot] == InInstanceId )
        {
            GamepadConnectionEvent event;
            event.Slot = slot;
            event.Connected = false;
            event.Name = GetName( slot );
            SDL_GameControllerClose( m_pads[slot] );
            m_pads[slot] = nullptr;
            m_instances[slot] = -1;
            CLog::Log( CLog::LogType::Info, "Gamepad " + std::to_string( slot ) + " disconnected" );
            event.Fire();
        }
    }
}


void Gamepads::CloseAll()
{
    for( int slot = 0; slot < kMaxGamepads; ++slot )
    {
        if( m_pads[slot] )
        {
            SDL_GameControllerClose( m_pads[slot] );
            m_pads[slot] = nullptr;
            m_instances[slot] = -1;
        }
    }
}


void Gamepads::Snapshot( std::array<GamepadState, kMaxGamepads>& OutState ) const
{
    for( int slot = 0; slot < kMaxGamepads; ++slot )
    {
        GamepadState& state = OutState[slot];
        state = GamepadState();
        SDL_GameController* pad = m_pads[slot];
        if( !pad || !SDL_GameControllerGetAttached( pad ) )
        {
            continue;
        }
        state.Connected = true;
        // GamepadButton / GamepadAxis follow SDL's order.
        for( int button = 0; button < static_cast<int>( GamepadButton::Count ); ++button )
        {
            state.Buttons[button] = SDL_GameControllerGetButton( pad, static_cast<SDL_GameControllerButton>( button ) ) != 0;
        }
        for( int axis = 0; axis < static_cast<int>( GamepadAxis::Count ); ++axis )
        {
            const float raw = std::max( SDL_GameControllerGetAxis( pad, static_cast<SDL_GameControllerAxis>( axis ) ) / 32767.f, -1.f );
            const bool flipY = axis == static_cast<int>( GamepadAxis::LeftY ) || axis == static_cast<int>( GamepadAxis::RightY );
            state.Axes[axis] = flipY ? -raw : raw;
        }
    }
}


int Gamepads::GetConnectedCount() const
{
    return static_cast<int>( std::count_if( m_pads.begin(), m_pads.end(), []( SDL_GameController* InPad ) { return InPad != nullptr; } ) );
}


bool Gamepads::IsConnected( int InSlot ) const
{
    return InSlot >= 0 && InSlot < kMaxGamepads && m_pads[InSlot];
}


std::string Gamepads::GetName( int InSlot ) const
{
    if( !IsConnected( InSlot ) )
    {
        return std::string();
    }
    const char* name = SDL_GameControllerName( m_pads[InSlot] );
    return name ? name : "Gamepad";
}


bool Gamepads::Rumble( int InSlot, float InLow, float InHigh, float InSeconds )
{
    return IsConnected( InSlot ) && SDL_GameControllerRumble( m_pads[InSlot], Motor( InLow ), Motor( InHigh ), Duration( InSeconds ) ) == 0;
}


bool Gamepads::RumbleTriggers( int InSlot, float InLeft, float InRight, float InSeconds )
{
    return IsConnected( InSlot ) && SDL_GameControllerRumbleTriggers( m_pads[InSlot], Motor( InLeft ), Motor( InRight ), Duration( InSeconds ) ) == 0;
}
