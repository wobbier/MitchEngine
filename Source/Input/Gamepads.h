#pragma once
#include "Events/Event.h"
#include "Input/InputTypes.h"
#include <array>
#include <string>

struct _SDL_GameController;

// Fired when a pad connects to or leaves a slot.
class GamepadConnectionEvent
    : public Event<GamepadConnectionEvent>
{
public:
    GamepadConnectionEvent()
        : Event()
    {
    }

    int Slot = 0;
    bool Connected = false;
    std::string Name;
};

// Game controllers through SDL's GameController API (Xbox, PlayStation, Switch and generic pads with
// a known mapping). Pads take the first free of kMaxGamepads stable slots as they connect (SDLWindow
// forwards the hotplug events) and keep it until they disconnect. Inputs snapshot their state.
class Gamepads
{
public:
    static Gamepads& Get();

    void OnDeviceAdded( int InDeviceIndex );
    void OnDeviceRemoved( int InInstanceId );
    void CloseAll();

    void Snapshot( std::array<GamepadState, kMaxGamepads>& OutState ) const;
    int GetConnectedCount() const;
    bool IsConnected( int InSlot ) const;
    std::string GetName( int InSlot ) const;

    // Low (left) and high (right) frequency motors, 0..1, for InSeconds. False if the pad can't.
    bool Rumble( int InSlot, float InLow, float InHigh, float InSeconds );
    bool RumbleTriggers( int InSlot, float InLeft, float InRight, float InSeconds );

private:
    Gamepads() = default;

    std::array<_SDL_GameController*, kMaxGamepads> m_pads{};
    std::array<int, kMaxGamepads> m_instances{ -1, -1, -1, -1 };
};
