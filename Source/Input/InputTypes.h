#pragma once
#include "Math/Vector2.h"
#include <array>
#include <cstdint>
#include <string>

// Gamepad buttons by position (Xbox A / PlayStation Cross is South).
enum class GamepadButton : uint8_t
{
    South = 0,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStickPress,
    RightStickPress,
    LeftShoulder,
    RightShoulder,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Misc,           // Xbox Share, PS5 microphone, Switch capture
    Paddle1,
    Paddle2,
    Paddle3,
    Paddle4,
    Touchpad,

    Count
};

enum class GamepadAxis : uint8_t
{
    LeftX = 0,
    LeftY,          // +1 = up (SDL's down-positive Y is flipped)
    RightX,
    RightY,         // +1 = up
    LeftTrigger,    // 0..1
    RightTrigger,   // 0..1

    Count
};

enum class GamepadStick : uint8_t
{
    Left = 0,
    Right,
};

constexpr int kMaxGamepads = 4;
constexpr int kKeyCount = 512;

const char* GamepadButtonName( GamepadButton InButton );
const char* GamepadAxisName( GamepadAxis InAxis );

struct GamepadState
{
    bool Connected = false;
    std::array<bool, static_cast<size_t>( GamepadButton::Count )> Buttons{};
    // Raw values: sticks -1..1 (Y up), triggers 0..1; deadzones apply when read.
    std::array<float, static_cast<size_t>( GamepadAxis::Count )> Axes{};
};

// One frame of raw device input; actions are evaluated from the current and previous snapshots.
struct InputDeviceState
{
    std::array<uint8_t, kKeyCount> Keys{};  // by SDL scancode / KeyCode
    uint32_t MouseButtons = 0;              // SDL_BUTTON() mask
    Vector2 MouseDelta;                     // pixels this frame, +Y up
    Vector2 Scroll;                         // wheel clicks this frame, +Y away from the user
    std::array<GamepadState, kMaxGamepads> Gamepads{};
};

// Stick and trigger shaping applied when gamepad values are read.
struct GamepadDeadzones
{
    float StickInner = 0.15f;   // radial: smaller deflections read 0, the rest rescales to 0..1
    float StickOuter = 0.95f;   // deflections past this read 1
    float Trigger = 0.08f;
};
