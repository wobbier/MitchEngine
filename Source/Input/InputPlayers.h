#pragma once
#include "Events/Event.h"
#include "Input/InputActions.h"
#include "Input/InputTypes.h"
#include <memory>
#include <vector>

// Fired when a player joins (a pad pressed the join button), loses its pad (unplugged) or gets it
// back (the pad reconnected to its slot).
class InputPlayerEvent
    : public Event<InputPlayerEvent>
{
public:
    enum class Change : uint8_t
    {
        Joined,
        DeviceLost,
        DeviceRegained,
    };

    InputPlayerEvent()
        : Event()
    {
    }

    int Player = 0;
    int GamepadSlot = -1;
    Change Type = Change::Joined;
};

// Local multiplayer. Player 0 is the default player (Input's actions): it reads the keyboard and
// mouse and any gamepad no other player owns, so a single-player game needs nothing from here.
// Further players get their own copy of the action map (their own contexts and rebinds) and their
// own devices: usually one pad each, paired explicitly or by pressing the join button.
class InputPlayers
{
public:
    static constexpr int kAnyGamepad = -1;    // any pad no other player owns
    static constexpr int kNoGamepad = -2;
    static constexpr int kMaxPlayers = 8;

    explicit InputPlayers( InputActionSystem& InPlayerZero );

    // A new player reading the given pad (and the keyboard and mouse if asked). Returns its index,
    // or -1 when kMaxPlayers already play.
    int AddPlayer( int InGamepadSlot = kNoGamepad, bool InKeyboardMouse = false );
    // Players above it move down one index. Player 0 can't be removed.
    void RemovePlayer( int InPlayer );
    int GetPlayerCount() const;
    // A player's actions (player 0 for an unknown index).
    InputActionSystem& GetActions( int InPlayer );
    const InputActionSystem& GetActions( int InPlayer ) const;

    void SetDevices( int InPlayer, bool InKeyboardMouse, int InGamepadSlot );
    int GetGamepad( int InPlayer ) const;
    bool HasKeyboardMouse( int InPlayer ) const;
    // The player whose pad is in this slot (owned, or read through kAnyGamepad by player 0), or -1.
    int GetPlayerForGamepad( int InSlot ) const;

    // Join on button press: a pad no player owns pressing InButton joins, becoming player 0's pad
    // while player 0 has none, then a new player (up to InMaxPlayers).
    void EnableJoining( int InMaxPlayers = 4, GamepadButton InButton = GamepadButton::South );
    void DisableJoining();
    bool IsJoining() const
    {
        return m_joining;
    }

    // Gives every player the map (their contexts and overrides survive).
    void SetMap( const InputActionMap& InMap );
    // Per frame: joins, pad loss / return, each player's devices, then every player's actions.
    void Update( const InputDeviceState& InDevices, const InputDeviceState& InPrevious, float InDeltaSeconds );

private:
    struct Player
    {
        InputActionSystem* Actions = nullptr;
        std::unique_ptr<InputActionSystem> Owned;
        bool KeyboardMouse = false;
        int Gamepad = kNoGamepad;
        bool DeviceLost = false;
    };

    uint8_t OwnedGamepads() const;
    void Notify( int InPlayer, int InSlot, InputPlayerEvent::Change InChange ) const;

    std::vector<Player> m_players;
    bool m_joining = false;
    int m_maxPlayers = 4;
    GamepadButton m_joinButton = GamepadButton::South;
};
