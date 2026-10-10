#include "PCH.h"
#include "InputActions.h"
#include "File.h"
#include "Path.h"
#include <SDL_mouse.h>
#include <SDL_scancode.h>
#include <SDL_keyboard.h>
#include <algorithm>
#include <cmath>

ME_REFLECT_ENUM( InputActionType, { { "Button", InputActionType::Button }, { "Value", InputActionType::Value }, { "Vector2", InputActionType::Vector2 } } )
ME_REFLECT_ENUM( InputBindingKind, { { "Control", InputBindingKind::Control }, { "Axis", InputBindingKind::Axis }, { "Vector2", InputBindingKind::Vector2 } } )

ME_REFLECT_BEGIN( InputBinding )
    ME_FIELD( Kind ).Tooltip( "Control: one control. Axis: Negative / Positive. Vector2: Up / Down / Left / Right" );
    ME_FIELD( Control ).Tooltip( "Keyboard/Space, Mouse/Left, Mouse/Delta, Gamepad/South, Gamepad/LeftStick, Gamepad/RightTrigger..." );
    ME_FIELD( Negative );
    ME_FIELD( Positive );
    ME_FIELD( Up );
    ME_FIELD( Down );
    ME_FIELD( Left );
    ME_FIELD( Right );
    ME_FIELD( Modifier ).Tooltip( "Optional control that must be held too, e.g. Keyboard/Left Ctrl" );
    ME_FIELD( Scale ).Range( -100.f, 100.f );
    ME_FIELD( InvertY );
ME_REFLECT_END()

ME_REFLECT_BEGIN( InputAction )
    ME_FIELD( Name );
    ME_FIELD( Type );
    ME_FIELD( PressThreshold ).Range( 0.01f, 1.f ).Tooltip( "Value at which the action counts as pressed" );
    ME_FIELD( Bindings );
ME_REFLECT_END()

ME_REFLECT_BEGIN( InputContext )
    ME_FIELD( Name );
    ME_FIELD( Priority ).Tooltip( "Higher contexts read input first" );
    ME_FIELD( EnabledByDefault );
    ME_FIELD( ConsumeInput ).Tooltip( "While enabled, controls bound here are hidden from lower-priority contexts" );
    ME_FIELD( Actions );
ME_REFLECT_END()

ME_REFLECT_BEGIN( InputActionMap )
    ME_FIELD( Contexts );
    ME_FIELD( StickDeadzone ).Category( "Gamepad" ).Range( 0.f, 0.9f );
    ME_FIELD( StickOuterDeadzone ).Category( "Gamepad" ).Range( 0.1f, 1.f );
    ME_FIELD( TriggerDeadzone ).Category( "Gamepad" ).Range( 0.f, 0.9f );
ME_REFLECT_END()

namespace
{
    enum ControlKind : uint8_t
    {
        None = 0,
        Key,
        MouseButton,
        MouseDelta,
        MouseScroll,
        PadButton,
        PadStick,
        PadTrigger,
        PadDPad,
    };

    enum ControlPart : uint8_t
    {
        Whole = 0,
        X,
        Y,
        HalfUp,
        HalfDown,
        HalfLeft,
        HalfRight,
    };

    const char* kMouseButtons[] = { "", "Left", "Middle", "Right", "X1", "X2" };

    const char* kGamepadButtons[] = { "South", "East", "West", "North", "Back", "Guide", "Start", "LeftStickPress", "RightStickPress", "LeftShoulder",
        "RightShoulder", "DPadUp", "DPadDown", "DPadLeft", "DPadRight", "Misc", "Paddle1", "Paddle2", "Paddle3", "Paddle4", "Touchpad" };
    static_assert( sizeof( kGamepadButtons ) / sizeof( kGamepadButtons[0] ) == static_cast<size_t>( GamepadButton::Count ), "One name per GamepadButton" );

    const char* kGamepadAxes[] = { "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };

    const InputActionState& EmptyState()
    {
        static const InputActionState state;
        return state;
    }


    float Magnitude( const Vector2& InValue, InputActionType InType )
    {
        return InType == InputActionType::Vector2 ? InValue.Length() : std::fabs( InValue.x );
    }
}


const char* GamepadButtonName( GamepadButton InButton )
{
    const size_t index = static_cast<size_t>( InButton );
    return index < static_cast<size_t>( GamepadButton::Count ) ? kGamepadButtons[index] : "";
}


const char* GamepadAxisName( GamepadAxis InAxis )
{
    const size_t index = static_cast<size_t>( InAxis );
    return index < static_cast<size_t>( GamepadAxis::Count ) ? kGamepadAxes[index] : "";
}


bool InputActionMap::LoadFromFile( const std::string& InPath )
{
    File file{ Path( InPath ) };
    const std::string& contents = file.Read();
    if( contents.empty() )
    {
        return false;
    }
    const json root = json::parse( contents, nullptr, false );
    if( root.is_discarded() || !root.is_object() )
    {
        YIKES( "Input actions: " + InPath + " is not valid JSON" );
        return false;
    }
    *this = InputActionMap();
    Reflection::FromJson( StaticType(), this, root );
    return true;
}


bool InputActionMap::SaveToFile( const std::string& InPath ) const
{
    json root;
    Reflection::ToJson( StaticType(), this, root );
    File file{ Path( InPath ) };
    file.Write( root.dump( 4 ) );
    return true;
}


float InputActionState::GetValue() const
{
    return Type == InputActionType::Vector2 ? Value.Length() : Value.x;
}


InputActionSystem::Control InputActionSystem::Parse( const std::string& InPath )
{
    Control control;
    const size_t slash = InPath.find( '/' );
    if( slash == std::string::npos )
    {
        return control;
    }
    const std::string device = InPath.substr( 0, slash );
    const std::string name = InPath.substr( slash + 1 );
    if( device == "Keyboard" )
    {
        const SDL_Scancode code = SDL_GetScancodeFromName( name.c_str() );
        if( code != SDL_SCANCODE_UNKNOWN && code < kKeyCount )
        {
            control.Kind = Key;
            control.Code = static_cast<uint16_t>( code );
        }
    }
    else if( device == "Mouse" )
    {
        for( uint16_t button = 1; button <= 5; ++button )
        {
            if( name == kMouseButtons[button] )
            {
                control.Kind = MouseButton;
                control.Code = button;
            }
        }
        const std::pair<const char*, std::pair<ControlKind, ControlPart>> axes[] = { { "Delta", { MouseDelta, Whole } }, { "DeltaX", { MouseDelta, X } }, { "DeltaY", { MouseDelta, Y } },
            { "Scroll", { MouseScroll, Whole } }, { "ScrollX", { MouseScroll, X } }, { "ScrollY", { MouseScroll, Y } } };
        for( const auto& axis : axes )
        {
            if( name == axis.first )
            {
                control.Kind = axis.second.first;
                control.Part = axis.second.second;
            }
        }
    }
    else if( device == "Gamepad" )
    {
        for( uint16_t button = 0; button < static_cast<uint16_t>( GamepadButton::Count ); ++button )
        {
            if( name == kGamepadButtons[button] )
            {
                control.Kind = PadButton;
                control.Code = button;
                return control;
            }
        }
        for( uint16_t stick = 0; stick < 2; ++stick )
        {
            const std::string prefix = stick == 0 ? "LeftStick" : "RightStick";
            if( name.rfind( prefix, 0 ) != 0 )
            {
                continue;
            }
            const std::string suffix = name.substr( prefix.size() );
            const std::pair<const char*, ControlPart> parts[] = { { "", Whole }, { "X", X }, { "Y", Y }, { "Up", HalfUp }, { "Down", HalfDown }, { "Left", HalfLeft }, { "Right", HalfRight } };
            for( const auto& part : parts )
            {
                if( suffix == part.first )
                {
                    control.Kind = PadStick;
                    control.Code = stick;
                    control.Part = part.second;
                }
            }
        }
        if( name == "LeftTrigger" || name == "RightTrigger" )
        {
            control.Kind = PadTrigger;
            control.Code = name == "LeftTrigger" ? 0 : 1;
        }
        if( name == "DPad" )
        {
            control.Kind = PadDPad;
        }
    }
    return control;
}


bool InputActionSystem::IsValidControl( const std::string& InControl )
{
    return Parse( InControl ).IsValid();
}


Vector2 InputActionSystem::Read( const Control& InControl, const InputDeviceState& InDevices ) const
{
    auto part = []( const Vector2& InValue, uint8_t InPart ) {
        switch( InPart )
        {
        case X:
            return Vector2( InValue.x, 0.f );
        case Y:
            return Vector2( InValue.y, 0.f );
        case HalfUp:
            return Vector2( std::max( InValue.y, 0.f ), 0.f );
        case HalfDown:
            return Vector2( std::max( -InValue.y, 0.f ), 0.f );
        case HalfLeft:
            return Vector2( std::max( -InValue.x, 0.f ), 0.f );
        case HalfRight:
            return Vector2( std::max( InValue.x, 0.f ), 0.f );
        default:
            return InValue;
        }
    };
    switch( InControl.Kind )
    {
    case Key:
    case MouseButton:
    case MouseDelta:
    case MouseScroll:
        if( !m_keyboardMouse )
        {
            return Vector2();   // another player's devices
        }
        break;
    default:
        break;
    }
    switch( InControl.Kind )
    {
    case Key:
        return Vector2( InDevices.Keys[InControl.Code] ? 1.f : 0.f, 0.f );
    case MouseButton:
        return Vector2( ( InDevices.MouseButtons & SDL_BUTTON( InControl.Code ) ) ? 1.f : 0.f, 0.f );
    case MouseDelta:
        return part( InDevices.MouseDelta, InControl.Part );
    case MouseScroll:
        return part( InDevices.Scroll, InControl.Part );
    default:
        break;
    }

    // Gamepads: the filtered slot, or the strongest of all connected pads.
    Vector2 strongest;
    for( int slot = 0; slot < kMaxGamepads; ++slot )
    {
        const GamepadState& pad = InDevices.Gamepads[slot];
        if( !pad.Connected || !( m_gamepadMask & ( 1u << slot ) ) )
        {
            continue;
        }
        Vector2 value;
        switch( InControl.Kind )
        {
        case PadButton:
            value = Vector2( pad.Buttons[InControl.Code] ? 1.f : 0.f, 0.f );
            break;
        case PadStick:
        {
            const size_t x = InControl.Code == 0 ? static_cast<size_t>( GamepadAxis::LeftX ) : static_cast<size_t>( GamepadAxis::RightX );
            value = part( ApplyStickDeadzone( Vector2( pad.Axes[x], pad.Axes[x + 1] ), m_map.StickDeadzone, m_map.StickOuterDeadzone ), InControl.Part );
            break;
        }
        case PadTrigger:
            value = Vector2( ApplyTriggerDeadzone( pad.Axes[InControl.Code == 0 ? static_cast<size_t>( GamepadAxis::LeftTrigger ) : static_cast<size_t>( GamepadAxis::RightTrigger )], m_map.TriggerDeadzone ), 0.f );
            break;
        case PadDPad:
        {
            auto button = [&pad]( GamepadButton InButton ) { return pad.Buttons[static_cast<size_t>( InButton )] ? 1.f : 0.f; };
            value = Vector2( button( GamepadButton::DPadRight ) - button( GamepadButton::DPadLeft ), button( GamepadButton::DPadUp ) - button( GamepadButton::DPadDown ) );
            if( value.Length() > 1.f )
            {
                value = value / value.Length();
            }
            break;
        }
        default:
            break;
        }
        if( value.Length() > strongest.Length() )
        {
            strongest = value;
        }
    }
    return strongest;
}


Vector2 InputActionSystem::ApplyStickDeadzone( const Vector2& InStick, float InInner, float InOuter )
{
    const float length = InStick.Length();
    if( length <= InInner || length <= 0.f )
    {
        return Vector2();
    }
    const float outer = std::max( InOuter, InInner + 0.01f );
    const float scaled = std::min( ( std::min( length, outer ) - InInner ) / ( outer - InInner ), 1.f );
    return InStick * ( scaled / length );
}


float InputActionSystem::ApplyTriggerDeadzone( float InTrigger, float InDeadzone )
{
    if( InTrigger <= InDeadzone )
    {
        return 0.f;
    }
    return std::min( ( InTrigger - InDeadzone ) / std::max( 1.f - InDeadzone, 0.01f ), 1.f );
}


Vector2 InputActionSystem::ReadBinding( const ResolvedBinding& InBinding, const InputDeviceState& InDevices, const std::vector<uint32_t>& InConsumed ) const
{
    auto consumed = [&InConsumed]( const Control& InControl ) {
        return InControl.IsValid() && std::find( InConsumed.begin(), InConsumed.end(), InControl.Key() ) != InConsumed.end();
    };
    if( consumed( InBinding.Main ) || consumed( InBinding.Negative ) || consumed( InBinding.Positive ) || consumed( InBinding.Up ) || consumed( InBinding.Down )
        || consumed( InBinding.Left ) || consumed( InBinding.Right ) || consumed( InBinding.Modifier ) )
    {
        return Vector2();
    }
    if( InBinding.Modifier.IsValid() && Read( InBinding.Modifier, InDevices ).Length() < 0.5f )
    {
        return Vector2();
    }
    auto strength = [this, &InDevices]( const Control& InControl ) {
        return InControl.IsValid() ? Read( InControl, InDevices ).Length() : 0.f;
    };
    Vector2 value;
    switch( InBinding.Kind )
    {
    case InputBindingKind::Control:
        value = InBinding.Main.IsValid() ? Read( InBinding.Main, InDevices ) : Vector2();
        break;
    case InputBindingKind::Axis:
        value = Vector2( strength( InBinding.Positive ) - strength( InBinding.Negative ), 0.f );
        break;
    case InputBindingKind::Vector2:
        value = Vector2( strength( InBinding.Right ) - strength( InBinding.Left ), strength( InBinding.Up ) - strength( InBinding.Down ) );
        if( value.Length() > 1.f )
        {
            value = value / value.Length();
        }
        break;
    }
    value = value * InBinding.Scale;
    if( InBinding.InvertY )
    {
        value.y = -value.y;
    }
    return value;
}


std::string InputActionSystem::OverrideKey( const std::string& InContext, const std::string& InAction, size_t InBinding, const std::string& InPart ) const
{
    return InContext + "/" + InAction + "/" + std::to_string( InBinding ) + ( InPart.empty() ? std::string() : "/" + InPart );
}


void InputActionSystem::SetMap( const InputActionMap& InMap )
{
    m_map = InMap;
    for( const InputContext& context : m_map.Contexts )
    {
        // Keep the game's enable/disable choices across hot reloads.
        m_contextEnabled.emplace( context.Name, context.EnabledByDefault );
    }
    m_states.clear();
    Resolve();
}


void InputActionSystem::Resolve()
{
    std::vector<const InputContext*> sorted;
    for( const InputContext& context : m_map.Contexts )
    {
        sorted.push_back( &context );
    }
    std::stable_sort( sorted.begin(), sorted.end(), []( const InputContext* InA, const InputContext* InB ) { return InA->Priority > InB->Priority; } );

    std::vector<InputActionState> previous = std::move( m_states );
    m_states.clear();
    m_contexts.clear();
    m_statesByName.clear();
    for( const InputContext* context : sorted )
    {
        ResolvedContext resolved;
        resolved.Name = context->Name;
        resolved.Priority = context->Priority;
        resolved.ConsumeInput = context->ConsumeInput;
        for( const InputAction& action : context->Actions )
        {
            ResolvedAction resolvedAction;
            resolvedAction.PressThreshold = action.PressThreshold;
            resolvedAction.State = m_states.size();
            for( size_t index = 0; index < action.Bindings.size(); ++index )
            {
                const InputBinding& binding = action.Bindings[index];
                auto control = [&]( const std::string& InPart, const std::string& InDefault ) {
                    auto found = m_overrides.find( OverrideKey( context->Name, action.Name, index, InPart ) );
                    return Parse( found != m_overrides.end() ? found->second : InDefault );
                };
                ResolvedBinding resolvedBinding;
                resolvedBinding.Kind = binding.Kind;
                resolvedBinding.Main = control( "", binding.Control );
                resolvedBinding.Negative = control( "Negative", binding.Negative );
                resolvedBinding.Positive = control( "Positive", binding.Positive );
                resolvedBinding.Up = control( "Up", binding.Up );
                resolvedBinding.Down = control( "Down", binding.Down );
                resolvedBinding.Left = control( "Left", binding.Left );
                resolvedBinding.Right = control( "Right", binding.Right );
                resolvedBinding.Modifier = Parse( binding.Modifier );
                resolvedBinding.Scale = binding.Scale;
                resolvedBinding.InvertY = binding.InvertY;
                resolvedAction.Bindings.push_back( resolvedBinding );
            }
            InputActionState state;
            state.Name = action.Name;
            state.Context = context->Name;
            state.Type = action.Type;
            for( const InputActionState& old : previous )
            {
                if( old.Name == state.Name && old.Context == state.Context )
                {
                    state = old;
                    state.Type = action.Type;
                    break;
                }
            }
            m_statesByName[action.Name].push_back( m_states.size() );
            m_states.push_back( state );
            resolved.Actions.push_back( std::move( resolvedAction ) );
        }
        m_contexts.push_back( std::move( resolved ) );
    }
}


void InputActionSystem::Update( const InputDeviceState& InDevices, float InDeltaSeconds )
{
    // Actions idle while rebinding, including the frame the new control is pressed.
    const bool rebinding = m_rebind.Active;
    if( m_rebind.Active )
    {
        const std::string actuated = FindActuatedControl( InDevices );
        if( !actuated.empty() )
        {
            Rebind rebind = std::move( m_rebind );
            m_rebind = Rebind();
            const Control control = Parse( actuated );
            const bool cancelled = std::any_of( rebind.Cancel.begin(), rebind.Cancel.end(), [&control]( const Control& InCancel ) { return InCancel.Key() == control.Key(); } );
            std::string bound;
            if( !cancelled )
            {
                bound = actuated;
                // A whole stick for a Vector2 binding, rather than the direction it was pushed.
                const InputActionState& state = GetAction( rebind.Action );
                if( rebind.Part.empty() && state.Type == InputActionType::Vector2 && control.Kind == PadStick )
                {
                    bound = control.Code == 0 ? "Gamepad/LeftStick" : "Gamepad/RightStick";
                }
                SetBindingOverride( rebind.Action, rebind.Binding, rebind.Part, bound );
            }
            if( rebind.Done )
            {
                rebind.Done( bound );
            }
        }
    }

    std::vector<uint32_t> consumed;
    for( const ResolvedContext& context : m_contexts )
    {
        const auto enabled = m_contextEnabled.find( context.Name );
        const bool isEnabled = ( enabled == m_contextEnabled.end() || enabled->second ) && !rebinding;
        for( const ResolvedAction& action : context.Actions )
        {
            InputActionState& state = m_states[action.State];
            Vector2 value;
            if( isEnabled )
            {
                for( const ResolvedBinding& binding : action.Bindings )
                {
                    const Vector2 candidate = ReadBinding( binding, InDevices, consumed );
                    if( Magnitude( candidate, state.Type ) > Magnitude( value, state.Type ) )
                    {
                        value = candidate;
                    }
                }
            }
            const bool wasPressed = state.Pressed;
            state.Value = state.Type == InputActionType::Vector2 ? value : Vector2( value.x, 0.f );
            state.Pressed = isEnabled && Magnitude( value, state.Type ) >= action.PressThreshold;
            state.PressedThisFrame = state.Pressed && !wasPressed;
            state.ReleasedThisFrame = !state.Pressed && wasPressed;
            state.HeldSeconds = state.Pressed ? state.HeldSeconds + InDeltaSeconds : 0.f;
        }
        if( isEnabled && context.ConsumeInput )
        {
            for( const ResolvedAction& action : context.Actions )
            {
                for( const ResolvedBinding& binding : action.Bindings )
                {
                    for( const Control* control : { &binding.Main, &binding.Negative, &binding.Positive, &binding.Up, &binding.Down, &binding.Left, &binding.Right } )
                    {
                        if( control->IsValid() )
                        {
                            consumed.push_back( control->Key() );
                        }
                    }
                }
            }
        }
    }
    m_previous = InDevices;
}


const InputActionState& InputActionSystem::GetAction( const std::string& InName ) const
{
    auto found = m_statesByName.find( InName );
    if( found == m_statesByName.end() || found->second.empty() )
    {
        return EmptyState();
    }
    for( size_t index : found->second )
    {
        if( IsContextEnabled( m_states[index].Context ) )
        {
            return m_states[index];
        }
    }
    return m_states[found->second.front()];
}


bool InputActionSystem::HasAction( const std::string& InName ) const
{
    return m_statesByName.find( InName ) != m_statesByName.end();
}


void InputActionSystem::SetContextEnabled( const std::string& InContext, bool InEnabled )
{
    m_contextEnabled[InContext] = InEnabled;
}


bool InputActionSystem::IsContextEnabled( const std::string& InContext ) const
{
    auto found = m_contextEnabled.find( InContext );
    return found == m_contextEnabled.end() || found->second;
}


bool InputActionSystem::SetBindingOverride( const std::string& InAction, size_t InBinding, const std::string& InPart, const std::string& InControl )
{
    if( !IsValidControl( InControl ) )
    {
        return false;
    }
    auto found = m_statesByName.find( InAction );
    if( found == m_statesByName.end() || found->second.empty() )
    {
        return false;
    }
    const InputActionState& state = m_states[found->second.front()];
    m_overrides[OverrideKey( state.Context, InAction, InBinding, InPart )] = InControl;
    Resolve();
    return true;
}


void InputActionSystem::ClearOverrides()
{
    m_overrides.clear();
    Resolve();
}


json InputActionSystem::SaveOverrides() const
{
    json overrides = json::object();
    for( const auto& entry : m_overrides )
    {
        overrides[entry.first] = entry.second;
    }
    return overrides;
}


void InputActionSystem::LoadOverrides( const json& InOverrides )
{
    m_overrides.clear();
    if( InOverrides.is_object() )
    {
        for( auto entry = InOverrides.begin(); entry != InOverrides.end(); ++entry )
        {
            if( entry.value().is_string() && IsValidControl( entry.value().get<std::string>() ) )
            {
                m_overrides[entry.key()] = entry.value().get<std::string>();
            }
        }
    }
    Resolve();
}


void InputActionSystem::StartInteractiveRebind( const std::string& InAction, size_t InBinding, const std::string& InPart, std::function<void( const std::string& )> InDone, std::vector<std::string> InCancel )
{
    CancelRebind();
    m_rebind.Active = true;
    m_rebind.Action = InAction;
    m_rebind.Binding = InBinding;
    m_rebind.Part = InPart;
    m_rebind.Done = std::move( InDone );
    for( const std::string& cancel : InCancel )
    {
        m_rebind.Cancel.push_back( Parse( cancel ) );
    }
}


void InputActionSystem::CancelRebind()
{
    if( !m_rebind.Active )
    {
        return;
    }
    Rebind rebind = std::move( m_rebind );
    m_rebind = Rebind();
    if( rebind.Done )
    {
        rebind.Done( "" );
    }
}


std::string InputActionSystem::FindActuatedControl( const InputDeviceState& InDevices ) const
{
    for( int key = 0; m_keyboardMouse && key < kKeyCount; ++key )
    {
        if( InDevices.Keys[key] && !m_previous.Keys[key] )
        {
            const char* name = SDL_GetScancodeName( static_cast<SDL_Scancode>( key ) );
            if( name && name[0] )
            {
                return std::string( "Keyboard/" ) + name;
            }
        }
    }
    for( uint32_t button = 1; m_keyboardMouse && button <= 5; ++button )
    {
        if( ( InDevices.MouseButtons & SDL_BUTTON( button ) ) && !( m_previous.MouseButtons & SDL_BUTTON( button ) ) )
        {
            return std::string( "Mouse/" ) + kMouseButtons[button];
        }
    }
    for( int slot = 0; slot < kMaxGamepads; ++slot )
    {
        const GamepadState& pad = InDevices.Gamepads[slot];
        const GamepadState& before = m_previous.Gamepads[slot];
        if( !pad.Connected || !( m_gamepadMask & ( 1u << slot ) ) )
        {
            continue;
        }
        for( size_t button = 0; button < pad.Buttons.size(); ++button )
        {
            if( pad.Buttons[button] && !before.Buttons[button] )
            {
                return std::string( "Gamepad/" ) + kGamepadButtons[button];
            }
        }
        for( size_t trigger : { static_cast<size_t>( GamepadAxis::LeftTrigger ), static_cast<size_t>( GamepadAxis::RightTrigger ) } )
        {
            if( pad.Axes[trigger] > 0.5f && before.Axes[trigger] <= 0.5f )
            {
                return std::string( "Gamepad/" ) + kGamepadAxes[trigger];
            }
        }
        for( int stick = 0; stick < 2; ++stick )
        {
            const size_t x = stick == 0 ? static_cast<size_t>( GamepadAxis::LeftX ) : static_cast<size_t>( GamepadAxis::RightX );
            const Vector2 now( pad.Axes[x], pad.Axes[x + 1] );
            const Vector2 then( before.Axes[x], before.Axes[x + 1] );
            if( now.Length() > 0.5f && then.Length() <= 0.5f )
            {
                const char* direction = std::fabs( now.x ) > std::fabs( now.y ) ? ( now.x > 0.f ? "Right" : "Left" ) : ( now.y > 0.f ? "Up" : "Down" );
                return std::string( "Gamepad/" ) + ( stick == 0 ? "LeftStick" : "RightStick" ) + direction;
            }
        }
    }
    return std::string();
}


std::string InputActionSystem::GetControlDisplay( const std::string& InControl )
{
    const size_t slash = InControl.find( '/' );
    if( slash == std::string::npos || !IsValidControl( InControl ) )
    {
        return InControl.empty() ? std::string() : "?";
    }
    const std::string device = InControl.substr( 0, slash );
    const std::string name = InControl.substr( slash + 1 );
    if( device == "Mouse" )
    {
        return name.rfind( "Delta", 0 ) == 0 || name.rfind( "Scroll", 0 ) == 0 ? "Mouse " + name : name + " Mouse";
    }
    if( device == "Gamepad" )
    {
        return "Gamepad " + name;
    }
    return name;
}


std::string InputActionSystem::GetBindingDisplay( const std::string& InAction, size_t InBinding ) const
{
    auto found = m_statesByName.find( InAction );
    if( found == m_statesByName.end() || found->second.empty() )
    {
        return std::string();
    }
    const InputActionState& state = m_states[found->second.front()];
    for( const InputContext& context : m_map.Contexts )
    {
        if( context.Name != state.Context )
        {
            continue;
        }
        for( const InputAction& action : context.Actions )
        {
            if( action.Name != InAction || InBinding >= action.Bindings.size() )
            {
                continue;
            }
            const InputBinding& binding = action.Bindings[InBinding];
            auto part = [&]( const std::string& InPart, const std::string& InDefault ) {
                auto overridden = m_overrides.find( OverrideKey( context.Name, InAction, InBinding, InPart ) );
                return GetControlDisplay( overridden != m_overrides.end() ? overridden->second : InDefault );
            };
            switch( binding.Kind )
            {
            case InputBindingKind::Axis:
                return part( "Negative", binding.Negative ) + "/" + part( "Positive", binding.Positive );
            case InputBindingKind::Vector2:
                return part( "Up", binding.Up ) + "/" + part( "Left", binding.Left ) + "/" + part( "Down", binding.Down ) + "/" + part( "Right", binding.Right );
            case InputBindingKind::Control:
            default:
                return part( "", binding.Control );
            }
        }
    }
    return std::string();
}
