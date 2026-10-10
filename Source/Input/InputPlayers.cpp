#include "PCH.h"
#include "InputPlayers.h"
#include "CLog.h"
#include <algorithm>

InputPlayers::InputPlayers( InputActionSystem& InPlayerZero )
{
    Player zero;
    zero.Actions = &InPlayerZero;
    zero.KeyboardMouse = true;
    zero.Gamepad = kAnyGamepad;
    m_players.push_back( std::move( zero ) );
}


int InputPlayers::AddPlayer( int InGamepadSlot, bool InKeyboardMouse )
{
    if( static_cast<int>( m_players.size() ) >= kMaxPlayers )
    {
        return -1;
    }
    Player player;
    player.Owned = std::make_unique<InputActionSystem>();
    player.Actions = player.Owned.get();
    if( m_players[0].Actions->HasMap() )
    {
        player.Actions->SetMap( m_players[0].Actions->GetMap() );
    }
    player.KeyboardMouse = InKeyboardMouse;
    player.Gamepad = InGamepadSlot;
    m_players.push_back( std::move( player ) );
    return static_cast<int>( m_players.size() ) - 1;
}


void InputPlayers::RemovePlayer( int InPlayer )
{
    if( InPlayer > 0 && InPlayer < static_cast<int>( m_players.size() ) )
    {
        m_players.erase( m_players.begin() + InPlayer );
    }
}


int InputPlayers::GetPlayerCount() const
{
    return static_cast<int>( m_players.size() );
}


InputActionSystem& InputPlayers::GetActions( int InPlayer )
{
    return InPlayer >= 0 && InPlayer < static_cast<int>( m_players.size() ) ? *m_players[InPlayer].Actions : *m_players[0].Actions;
}


const InputActionSystem& InputPlayers::GetActions( int InPlayer ) const
{
    return InPlayer >= 0 && InPlayer < static_cast<int>( m_players.size() ) ? *m_players[InPlayer].Actions : *m_players[0].Actions;
}


void InputPlayers::SetDevices( int InPlayer, bool InKeyboardMouse, int InGamepadSlot )
{
    if( InPlayer >= 0 && InPlayer < static_cast<int>( m_players.size() ) )
    {
        m_players[InPlayer].KeyboardMouse = InKeyboardMouse;
        m_players[InPlayer].Gamepad = InGamepadSlot;
        m_players[InPlayer].DeviceLost = false;
    }
}


int InputPlayers::GetGamepad( int InPlayer ) const
{
    return InPlayer >= 0 && InPlayer < static_cast<int>( m_players.size() ) ? m_players[InPlayer].Gamepad : kNoGamepad;
}


bool InputPlayers::HasKeyboardMouse( int InPlayer ) const
{
    return InPlayer >= 0 && InPlayer < static_cast<int>( m_players.size() ) && m_players[InPlayer].KeyboardMouse;
}


int InputPlayers::GetPlayerForGamepad( int InSlot ) const
{
    for( size_t i = 0; i < m_players.size(); ++i )
    {
        if( m_players[i].Gamepad == InSlot )
        {
            return static_cast<int>( i );
        }
    }
    if( InSlot >= 0 && InSlot < kMaxGamepads && !( OwnedGamepads() & ( 1u << InSlot ) ) )
    {
        for( size_t i = 0; i < m_players.size(); ++i )
        {
            if( m_players[i].Gamepad == kAnyGamepad )
            {
                return static_cast<int>( i );
            }
        }
    }
    return -1;
}


void InputPlayers::EnableJoining( int InMaxPlayers, GamepadButton InButton )
{
    m_joining = true;
    m_maxPlayers = std::clamp( InMaxPlayers, 1, kMaxPlayers );
    m_joinButton = InButton;
}


void InputPlayers::DisableJoining()
{
    m_joining = false;
}


void InputPlayers::SetMap( const InputActionMap& InMap )
{
    for( Player& player : m_players )
    {
        player.Actions->SetMap( InMap );
    }
}


uint8_t InputPlayers::OwnedGamepads() const
{
    uint8_t owned = 0;
    for( const Player& player : m_players )
    {
        if( player.Gamepad >= 0 && player.Gamepad < kMaxGamepads )
        {
            owned |= static_cast<uint8_t>( 1u << player.Gamepad );
        }
    }
    return owned;
}


void InputPlayers::Notify( int InPlayer, int InSlot, InputPlayerEvent::Change InChange ) const
{
    static const char* kChanges[] = { "joined", "lost its gamepad", "got its gamepad back" };
    CLog::Log( CLog::LogType::Info, "Input: player " + std::to_string( InPlayer ) + " " + kChanges[static_cast<int>( InChange )] + " (slot " + std::to_string( InSlot ) + ")" );
    InputPlayerEvent event;
    event.Player = InPlayer;
    event.GamepadSlot = InSlot;
    event.Type = InChange;
    event.Fire();
}


void InputPlayers::Update( const InputDeviceState& InDevices, const InputDeviceState& InPrevious, float InDeltaSeconds )
{
    // Joining: an unowned pad pressing the join button.
    if( m_joining )
    {
        const size_t button = static_cast<size_t>( m_joinButton );
        for( int slot = 0; slot < kMaxGamepads; ++slot )
        {
            const GamepadState& pad = InDevices.Gamepads[slot];
            const bool pressed = pad.Connected && pad.Buttons[button] && !InPrevious.Gamepads[slot].Buttons[button];
            if( !pressed || ( OwnedGamepads() & ( 1u << slot ) ) )
            {
                continue;
            }
            if( m_players[0].Gamepad == kAnyGamepad )
            {
                m_players[0].Gamepad = slot;
                Notify( 0, slot, InputPlayerEvent::Change::Joined );
            }
            else if( GetPlayerCount() < m_maxPlayers )
            {
                const int player = AddPlayer( slot, false );
                if( player >= 0 )
                {
                    Notify( player, slot, InputPlayerEvent::Change::Joined );
                }
            }
        }
    }

    // Pads coming and going under their players, and which devices each player reads.
    const uint8_t owned = OwnedGamepads();
    for( size_t i = 0; i < m_players.size(); ++i )
    {
        Player& player = m_players[i];
        uint8_t mask = 0;
        if( player.Gamepad >= 0 && player.Gamepad < kMaxGamepads )
        {
            mask = static_cast<uint8_t>( 1u << player.Gamepad );
            const bool connected = InDevices.Gamepads[player.Gamepad].Connected;
            if( !connected && !player.DeviceLost )
            {
                player.DeviceLost = true;
                Notify( static_cast<int>( i ), player.Gamepad, InputPlayerEvent::Change::DeviceLost );
            }
            else if( connected && player.DeviceLost )
            {
                player.DeviceLost = false;
                Notify( static_cast<int>( i ), player.Gamepad, InputPlayerEvent::Change::DeviceRegained );
            }
        }
        else if( player.Gamepad == kAnyGamepad )
        {
            mask = static_cast<uint8_t>( ~owned & ( ( 1u << kMaxGamepads ) - 1 ) );
        }
        player.Actions->SetDevices( player.KeyboardMouse, mask );
        player.Actions->Update( InDevices, InDeltaSeconds );
    }
}
