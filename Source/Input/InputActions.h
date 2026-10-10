#pragma once
#include "Input/InputTypes.h"
#include "JSON.h"
#include "Math/Vector2.h"
#include "Reflection/Reflection.h"
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// Action maps: named actions ("Jump", "Move") bound to device controls, grouped in contexts that
// games enable and disable ("Gameplay", "Menu", "Vehicle"). Bindings name controls by path:
//
//   Keyboard/<SDL key name>        Keyboard/Space, Keyboard/W, Keyboard/Left Shift
//   Mouse/Left|Right|Middle|X1|X2   Mouse/Delta (Vector2, +Y up), Mouse/DeltaX|DeltaY, Mouse/Scroll, Mouse/ScrollY
//   Gamepad/<button>                Gamepad/South (A / Cross), Gamepad/Start, Gamepad/DPadUp, ...
//   Gamepad/LeftStick|RightStick    Vector2 with deadzones (+Y up); also LeftStickX/Y, LeftStickUp/Down/Left/Right
//   Gamepad/LeftTrigger|RightTrigger  0..1;  Gamepad/DPad  Vector2 from the d-pad buttons
//
// A binding is one control, a 1D composite (Negative / Positive) or a 2D composite (Up / Down /
// Left / Right, normalized). An action's value is its strongest binding's.

enum class InputActionType : uint8_t
{
    Button = 0,     // pressed when the value reaches PressThreshold
    Value,          // a float (triggers, 1D composites)
    Vector2,        // sticks, mouse delta, WASD
};

enum class InputBindingKind : uint8_t
{
    Control = 0,
    Axis,           // Negative / Positive controls -> -1..1
    Vector2,        // Up / Down / Left / Right controls -> a normalized Vector2
};

struct InputBinding
{
    ME_REFLECTABLE( InputBinding )
public:
    InputBindingKind Kind = InputBindingKind::Control;
    std::string Control;
    std::string Negative;
    std::string Positive;
    std::string Up;
    std::string Down;
    std::string Left;
    std::string Right;
    std::string Modifier;       // optional control that must be held too (e.g. Keyboard/Left Ctrl)
    float Scale = 1.f;
    bool InvertY = false;
};

struct InputAction
{
    ME_REFLECTABLE( InputAction )
public:
    std::string Name;
    InputActionType Type = InputActionType::Button;
    float PressThreshold = 0.5f;
    std::vector<InputBinding> Bindings;
};

struct InputContext
{
    ME_REFLECTABLE( InputContext )
public:
    std::string Name;
    int Priority = 0;           // higher contexts read input first
    bool EnabledByDefault = true;
    bool ConsumeInput = false;  // controls bound here are hidden from lower contexts while enabled
    std::vector<InputAction> Actions;
};

// The contents of a .inputactions asset.
struct InputActionMap
{
    ME_REFLECTABLE( InputActionMap )
public:
    std::vector<InputContext> Contexts;
    float StickDeadzone = 0.15f;
    float StickOuterDeadzone = 0.95f;
    float TriggerDeadzone = 0.08f;

    bool LoadFromFile( const std::string& InPath );
    bool SaveToFile( const std::string& InPath ) const;
};

// The live state of one action.
struct InputActionState
{
    std::string Name;
    std::string Context;
    InputActionType Type = InputActionType::Button;
    Vector2 Value;
    bool Pressed = false;
    bool PressedThisFrame = false;
    bool ReleasedThisFrame = false;
    float HeldSeconds = 0.f;

    // Button / Value: the value; Vector2: its length.
    float GetValue() const;
    Vector2 GetVector2() const
    {
        return Value;
    }
    bool IsPressed() const
    {
        return Pressed;
    }
    bool WasPressed() const
    {
        return PressedThisFrame;
    }
    bool WasReleased() const
    {
        return ReleasedThisFrame;
    }
};

// Evaluates an action map against device snapshots.
class InputActionSystem
{
public:
    void SetMap( const InputActionMap& InMap );
    const InputActionMap& GetMap() const
    {
        return m_map;
    }
    bool HasMap() const
    {
        return !m_map.Contexts.empty();
    }

    // Reads every action from this frame's device state.
    void Update( const InputDeviceState& InDevices, float InDeltaSeconds );

    // The action's state in the highest-priority enabled context that has it (a zero state if none).
    const InputActionState& GetAction( const std::string& InName ) const;
    bool HasAction( const std::string& InName ) const;

    void SetContextEnabled( const std::string& InContext, bool InEnabled );
    bool IsContextEnabled( const std::string& InContext ) const;

    // Which gamepad slot "Gamepad/..." controls read: -1 = any (the strongest), else that slot.
    void SetGamepadFilter( int InSlot )
    {
        m_gamepadMask = InSlot >= 0 && InSlot < kMaxGamepads ? static_cast<uint8_t>( 1u << InSlot ) : 0xFF;
    }
    // The devices this system reads (local multiplayer, see InputPlayers): the keyboard and mouse,
    // and the gamepad slots in the mask (bit n = slot n; the strongest of them wins).
    void SetDevices( bool InKeyboardMouse, uint8_t InGamepadMask )
    {
        m_keyboardMouse = InKeyboardMouse;
        m_gamepadMask = InGamepadMask;
    }
    bool ReadsKeyboardMouse() const
    {
        return m_keyboardMouse;
    }
    uint8_t GetGamepadMask() const
    {
        return m_gamepadMask;
    }

    // Rebinding: overrides replace a binding's control (or a composite part: "Up", "Negative"...)
    // and survive SetMap; Save/LoadOverrides persist them as JSON.
    bool SetBindingOverride( const std::string& InAction, size_t InBinding, const std::string& InPart, const std::string& InControl );
    void ClearOverrides();
    json SaveOverrides() const;
    void LoadOverrides( const json& InOverrides );
    // Waits for the next control to be actuated (a key, button, or an axis pushed past halfway)
    // and binds it; InCancel controls (default Escape) cancel. The callback gets the control path,
    // or an empty string when cancelled.
    void StartInteractiveRebind( const std::string& InAction, size_t InBinding, const std::string& InPart, std::function<void( const std::string& )> InDone, std::vector<std::string> InCancel = { "Keyboard/Escape" } );
    bool IsRebinding() const
    {
        return m_rebind.Active;
    }
    void CancelRebind();

    // A short label for UI prompts: "Space", "Left Mouse", "Gamepad South", "W/A/S/D".
    std::string GetBindingDisplay( const std::string& InAction, size_t InBinding ) const;
    static std::string GetControlDisplay( const std::string& InControl );
    // True if the path names a known control.
    static bool IsValidControl( const std::string& InControl );

    // Radial stick deadzone: inside InInner reads 0, the rest rescales so InOuter and beyond read 1.
    static Vector2 ApplyStickDeadzone( const Vector2& InStick, float InInner, float InOuter );
    static float ApplyTriggerDeadzone( float InTrigger, float InDeadzone );

private:
    struct Control
    {
        uint8_t Kind = 0;
        uint16_t Code = 0;
        uint8_t Part = 0;   // 0 = whole value, 1 = x, 2 = y, 3..6 = up/down/left/right half axes
        bool IsValid() const
        {
            return Kind != 0;
        }
        uint32_t Key() const
        {
            return ( static_cast<uint32_t>( Kind ) << 24 ) | ( static_cast<uint32_t>( Code ) << 8 ) | Part;
        }
    };
    struct ResolvedBinding
    {
        InputBindingKind Kind = InputBindingKind::Control;
        Control Main;
        Control Negative, Positive;
        Control Up, Down, Left, Right;
        Control Modifier;
        float Scale = 1.f;
        bool InvertY = false;
    };
    struct ResolvedAction
    {
        std::vector<ResolvedBinding> Bindings;
        float PressThreshold = 0.5f;
        size_t State = 0;
    };
    struct ResolvedContext
    {
        std::string Name;
        int Priority = 0;
        bool Enabled = true;
        bool ConsumeInput = false;
        std::vector<ResolvedAction> Actions;
    };
    struct Rebind
    {
        bool Active = false;
        std::string Action;
        size_t Binding = 0;
        std::string Part;
        std::vector<Control> Cancel;
        std::function<void( const std::string& )> Done;
    };

    static Control Parse( const std::string& InPath );
    Vector2 Read( const Control& InControl, const InputDeviceState& InDevices ) const;
    Vector2 ReadBinding( const ResolvedBinding& InBinding, const InputDeviceState& InDevices, const std::vector<uint32_t>& InConsumed ) const;
    void Resolve();
    std::string FindActuatedControl( const InputDeviceState& InDevices ) const;
    std::string OverrideKey( const std::string& InContext, const std::string& InAction, size_t InBinding, const std::string& InPart ) const;

    InputActionMap m_map;
    std::vector<ResolvedContext> m_contexts;     // sorted by priority, highest first
    std::vector<InputActionState> m_states;
    std::unordered_map<std::string, std::vector<size_t>> m_statesByName;   // in context priority order
    std::map<std::string, bool> m_contextEnabled;
    std::map<std::string, std::string> m_overrides;
    InputDeviceState m_previous;
    bool m_keyboardMouse = true;
    uint8_t m_gamepadMask = 0xFF;
    Rebind m_rebind;
};
