#include <doctest/doctest.h>
#include "Input/InputPlayers.h"
#include "Events/EventReceiver.h"
#include "Input/InputActions.h"
#include <SDL_keyboard.h>
#include <SDL_mouse.h>
#include <filesystem>

namespace InputTest
{
    InputBinding Bind( const std::string& InControl, const std::string& InModifier = "" )
    {
        InputBinding binding;
        binding.Control = InControl;
        binding.Modifier = InModifier;
        return binding;
    }

    InputBinding Wasd()
    {
        InputBinding binding;
        binding.Kind = InputBindingKind::Vector2;
        binding.Up = "Keyboard/W";
        binding.Down = "Keyboard/S";
        binding.Left = "Keyboard/A";
        binding.Right = "Keyboard/D";
        return binding;
    }

    InputAction Action( const std::string& InName, InputActionType InType, std::vector<InputBinding> InBindings )
    {
        InputAction action;
        action.Name = InName;
        action.Type = InType;
        action.Bindings = std::move( InBindings );
        return action;
    }

    InputActionMap GameMap()
    {
        InputContext gameplay;
        gameplay.Name = "Gameplay";
        gameplay.Actions = {
            Action( "Jump", InputActionType::Button, { Bind( "Keyboard/Space" ), Bind( "Gamepad/South" ) } ),
            Action( "Move", InputActionType::Vector2, { Wasd(), Bind( "Gamepad/LeftStick" ) } ),
            Action( "Pause", InputActionType::Button, { Bind( "Keyboard/Escape" ) } ),
            Action( "Save", InputActionType::Button, { Bind( "Keyboard/S", "Keyboard/Left Ctrl" ) } ),
            Action( "Fire", InputActionType::Value, { Bind( "Gamepad/RightTrigger" ), Bind( "Mouse/Left" ) } ),
        };
        InputContext menu;
        menu.Name = "Menu";
        menu.Priority = 10;
        menu.ConsumeInput = true;
        menu.EnabledByDefault = false;
        menu.Actions = { Action( "Back", InputActionType::Button, { Bind( "Keyboard/Escape" ), Bind( "Gamepad/East" ) } ) };
        InputActionMap map;
        map.Contexts = { gameplay, menu };
        return map;
    }

    void Key( InputDeviceState& InOutState, const char* InName, bool InDown )
    {
        InOutState.Keys[SDL_GetScancodeFromName( InName )] = InDown ? 1 : 0;
    }
}

using namespace InputTest;

TEST_CASE( "Input: control paths parse, and display for prompts" )
{
    CHECK( InputActionSystem::IsValidControl( "Keyboard/Space" ) );
    CHECK( InputActionSystem::IsValidControl( "Keyboard/Left Shift" ) );
    CHECK( InputActionSystem::IsValidControl( "Mouse/Delta" ) );
    CHECK( InputActionSystem::IsValidControl( "Gamepad/LeftStickUp" ) );
    CHECK( InputActionSystem::IsValidControl( "Gamepad/RightTrigger" ) );
    CHECK_FALSE( InputActionSystem::IsValidControl( "Keyboard/NotAKey" ) );
    CHECK_FALSE( InputActionSystem::IsValidControl( "Gamepad/Banana" ) );
    CHECK_FALSE( InputActionSystem::IsValidControl( "Space" ) );
    CHECK( InputActionSystem::GetControlDisplay( "Mouse/Left" ) == "Left Mouse" );
    CHECK( InputActionSystem::GetControlDisplay( "Gamepad/South" ) == "Gamepad South" );

    InputActionSystem actions;
    actions.SetMap( GameMap() );
    CHECK( actions.GetBindingDisplay( "Move", 0 ) == "W/A/S/D" );
    CHECK( actions.GetBindingDisplay( "Jump", 1 ) == "Gamepad South" );
}

TEST_CASE( "Input: buttons press, hold and release across frames" )
{
    InputActionSystem actions;
    actions.SetMap( GameMap() );
    InputDeviceState devices;
    actions.Update( devices, 0.016f );
    CHECK_FALSE( actions.GetAction( "Jump" ).IsPressed() );

    Key( devices, "Space", true );
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Jump" ).WasPressed() );
    CHECK( actions.GetAction( "Jump" ).IsPressed() );
    actions.Update( devices, 0.5f );
    CHECK_FALSE( actions.GetAction( "Jump" ).WasPressed() );
    CHECK( actions.GetAction( "Jump" ).HeldSeconds == doctest::Approx( 0.516f ) );

    Key( devices, "Space", false );
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Jump" ).WasReleased() );
    CHECK_FALSE( actions.GetAction( "Jump" ).IsPressed() );

    // The same action from a gamepad; unknown actions read as a zero state.
    devices.Gamepads[2].Connected = true;
    devices.Gamepads[2].Buttons[static_cast<size_t>( GamepadButton::South )] = true;
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Jump" ).WasPressed() );
    CHECK_FALSE( actions.GetAction( "Nope" ).IsPressed() );

    // Pinning the gamepad slot ignores other pads.
    actions.SetGamepadFilter( 0 );
    actions.Update( devices, 0.016f );
    CHECK_FALSE( actions.GetAction( "Jump" ).IsPressed() );
}

TEST_CASE( "Input: composites normalize, sticks have deadzones, the strongest binding wins" )
{
    InputActionSystem actions;
    actions.SetMap( GameMap() );
    InputDeviceState devices;
    Key( devices, "W", true );
    Key( devices, "D", true );
    actions.Update( devices, 0.016f );
    const Vector2 diagonal = actions.GetAction( "Move" ).GetVector2();
    CHECK( diagonal.x == doctest::Approx( 0.7071f ) );
    CHECK( diagonal.y == doctest::Approx( 0.7071f ) );
    CHECK( actions.GetAction( "Move" ).GetValue() == doctest::Approx( 1.f ) );

    Key( devices, "W", false );
    Key( devices, "D", false );
    devices.Gamepads[0].Connected = true;
    devices.Gamepads[0].Axes[static_cast<size_t>( GamepadAxis::LeftX )] = 0.1f;   // inside the deadzone
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Move" ).GetValue() == doctest::Approx( 0.f ) );
    devices.Gamepads[0].Axes[static_cast<size_t>( GamepadAxis::LeftX )] = 0.55f;
    actions.Update( devices, 0.016f );
    // (0.55 - 0.15) / (0.95 - 0.15)
    CHECK( actions.GetAction( "Move" ).GetVector2().x == doctest::Approx( 0.5f ) );
    devices.Gamepads[0].Axes[static_cast<size_t>( GamepadAxis::LeftY )] = 1.f;
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Move" ).GetValue() == doctest::Approx( 1.f ) );   // clamped past the outer deadzone

    // Keys at full strength beat a half-pushed stick.
    devices.Gamepads[0].Axes = {};
    devices.Gamepads[0].Axes[static_cast<size_t>( GamepadAxis::LeftX )] = -0.55f;
    Key( devices, "D", true );
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Move" ).GetVector2().x == doctest::Approx( 1.f ) );

    // Triggers: deadzone, then 0..1.
    devices.Gamepads[0].Axes[static_cast<size_t>( GamepadAxis::RightTrigger )] = 0.05f;
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Fire" ).GetValue() == doctest::Approx( 0.f ) );
    devices.Gamepads[0].Axes[static_cast<size_t>( GamepadAxis::RightTrigger )] = 1.f;
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Fire" ).GetValue() == doctest::Approx( 1.f ) );
    CHECK( actions.GetAction( "Fire" ).IsPressed() );
}

TEST_CASE( "Input: modifiers, and higher-priority contexts consume their controls" )
{
    InputActionSystem actions;
    actions.SetMap( GameMap() );
    InputDeviceState devices;
    Key( devices, "S", true );
    actions.Update( devices, 0.016f );
    CHECK_FALSE( actions.GetAction( "Save" ).IsPressed() );
    CHECK( actions.GetAction( "Move" ).GetVector2().y == doctest::Approx( -1.f ) );
    Key( devices, "Left Ctrl", true );
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Save" ).WasPressed() );
    Key( devices, "S", false );
    Key( devices, "Left Ctrl", false );

    // Menu off: Escape pauses. Menu on: Escape goes back, and Gameplay no longer sees it.
    Key( devices, "Escape", true );
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Pause" ).IsPressed() );
    CHECK_FALSE( actions.GetAction( "Back" ).IsPressed() );
    CHECK_FALSE( actions.IsContextEnabled( "Menu" ) );

    actions.SetContextEnabled( "Menu", true );
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Back" ).WasPressed() );
    CHECK_FALSE( actions.GetAction( "Pause" ).IsPressed() );
    CHECK( actions.GetAction( "Pause" ).WasReleased() );
    // Controls the menu doesn't bind still reach gameplay.
    Key( devices, "W", true );
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Move" ).GetVector2().y == doctest::Approx( 1.f ) );

    // Disabling a context releases its actions.
    actions.SetContextEnabled( "Gameplay", false );
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Move" ).GetValue() == doctest::Approx( 0.f ) );
}

TEST_CASE( "Input: interactive rebinding, overrides that persist, and map files" )
{
    InputActionSystem actions;
    actions.SetMap( GameMap() );
    InputDeviceState devices;
    actions.Update( devices, 0.016f );

    std::string bound = "pending";
    actions.StartInteractiveRebind( "Jump", 0, "", [&bound]( const std::string& InControl ) { bound = InControl; } );
    CHECK( actions.IsRebinding() );
    Key( devices, "K", true );
    actions.Update( devices, 0.016f );
    CHECK( bound == "Keyboard/K" );
    CHECK_FALSE( actions.IsRebinding() );
    CHECK_FALSE( actions.GetAction( "Jump" ).IsPressed() );   // actions are idle while rebinding
    actions.Update( devices, 0.016f );
    CHECK( actions.GetAction( "Jump" ).IsPressed() );
    CHECK( actions.GetBindingDisplay( "Jump", 0 ) == "K" );
    Key( devices, "K", false );
    Key( devices, "Space", true );
    actions.Update( devices, 0.016f );
    CHECK_FALSE( actions.GetAction( "Jump" ).IsPressed() );

    // Escape cancels and keeps the binding.
    actions.StartInteractiveRebind( "Jump", 0, "", [&bound]( const std::string& InControl ) { bound = InControl; } );
    Key( devices, "Escape", true );
    actions.Update( devices, 0.016f );
    CHECK( bound.empty() );
    CHECK( actions.GetBindingDisplay( "Jump", 0 ) == "K" );

    // Composite parts rebind individually; overrides survive a reload of the map and persist.
    CHECK( actions.SetBindingOverride( "Move", 0, "Up", "Keyboard/Up" ) );
    CHECK_FALSE( actions.SetBindingOverride( "Move", 0, "Up", "Keyboard/NotAKey" ) );
    const json saved = actions.SaveOverrides();
    InputActionSystem restored;
    restored.SetMap( GameMap() );
    restored.LoadOverrides( saved );
    CHECK( restored.GetBindingDisplay( "Move", 0 ) == "Up/A/S/D" );
    CHECK( restored.GetBindingDisplay( "Jump", 0 ) == "K" );
    restored.ClearOverrides();
    CHECK( restored.GetBindingDisplay( "Jump", 0 ) == "Space" );

    // Maps round-trip through .inputactions files.
    std::filesystem::create_directories( ".tmp/Tests" );
    REQUIRE( GameMap().SaveToFile( ".tmp/Tests/Test.inputactions" ) );
    InputActionMap loaded;
    REQUIRE( loaded.LoadFromFile( ".tmp/Tests/Test.inputactions" ) );
    REQUIRE( loaded.Contexts.size() == 2 );
    CHECK( loaded.Contexts[1].ConsumeInput );
    CHECK( loaded.Contexts[0].Actions[1].Bindings[0].Kind == InputBindingKind::Vector2 );
    CHECK( loaded.Contexts[0].Actions[3].Bindings[0].Modifier == "Keyboard/Left Ctrl" );
}


namespace InputPlayersTest
{
    struct PlayerLog
        : public EventReceiver
    {
        PlayerLog()
        {
            EventManager::GetInstance().RegisterReceiver( this, { InputPlayerEvent::GetEventId() } );
        }

        bool OnEvent( const BaseEvent& InEvent ) override
        {
            const InputPlayerEvent& event = static_cast<const InputPlayerEvent&>( InEvent );
            Events.push_back( { event.Player, event.GamepadSlot, event.Type } );
            return false;
        }

        struct Entry
        {
            int Player;
            int Slot;
            InputPlayerEvent::Change Type;
        };
        std::vector<Entry> Events;
    };

    InputActionMap JumpMap()
    {
        InputAction jump;
        jump.Name = "Jump";
        InputBinding pad;
        pad.Control = "Gamepad/South";
        InputBinding key;
        key.Control = "Keyboard/Space";
        jump.Bindings = { pad, key };
        InputContext context;
        context.Name = "Gameplay";
        context.Actions = { jump };
        InputActionMap map;
        map.Contexts = { context };
        return map;
    }
}

TEST_CASE( "Input: local players pair pads, join on button press and keep their own devices" )
{
    using namespace InputPlayersTest;
    PlayerLog log;
    InputActionSystem playerZero;
    InputPlayers players( playerZero );
    players.SetMap( JumpMap() );

    InputDeviceState previous;
    InputDeviceState devices;
    for( int slot = 0; slot < kMaxGamepads; ++slot )
    {
        devices.Gamepads[slot].Connected = true;
    }
    const size_t south = static_cast<size_t>( GamepadButton::South );
    const int space = SDL_SCANCODE_SPACE;
    auto frame = [&]() {
        players.Update( devices, previous, 1.f / 60.f );
        previous = devices;
    };
    auto press = [&]( int InSlot ) {
        devices.Gamepads[InSlot].Buttons[south] = true;
        frame();
        devices.Gamepads[InSlot].Buttons[south] = false;
    };

    // Alone, player 0 reads every pad and the keyboard: single-player games need nothing.
    devices.Gamepads[2].Buttons[south] = true;
    frame();
    CHECK( players.GetActions( 0 ).GetAction( "Jump" ).IsPressed() );
    devices.Gamepads[2].Buttons[south] = false;
    frame();

    // Joining: the first pad becomes player 0's, the next ones new players.
    players.EnableJoining( 3 );
    press( 1 );
    frame();
    CHECK( players.GetPlayerCount() == 1 );
    CHECK( players.GetGamepad( 0 ) == 1 );
    press( 2 );
    frame();
    REQUIRE( players.GetPlayerCount() == 2 );
    CHECK( players.GetGamepad( 1 ) == 2 );
    CHECK_FALSE( players.HasKeyboardMouse( 1 ) );
    REQUIRE( log.Events.size() == 2 );
    CHECK( log.Events[1].Player == 1 );
    CHECK( log.Events[1].Type == InputPlayerEvent::Change::Joined );

    // Each player hears only its own devices.
    devices.Gamepads[2].Buttons[south] = true;
    frame();
    CHECK( players.GetActions( 1 ).GetAction( "Jump" ).IsPressed() );
    CHECK_FALSE( players.GetActions( 0 ).GetAction( "Jump" ).IsPressed() );
    devices.Gamepads[2].Buttons[south] = false;
    devices.Keys[space] = 1;
    frame();
    CHECK( players.GetActions( 0 ).GetAction( "Jump" ).IsPressed() );
    CHECK_FALSE( players.GetActions( 1 ).GetAction( "Jump" ).IsPressed() );
    devices.Keys[space] = 0;
    frame();

    // Up to the limit: a third player joins, a fourth pad doesn't.
    press( 3 );
    frame();
    CHECK( players.GetPlayerCount() == 3 );
    press( 0 );
    frame();
    CHECK( players.GetPlayerCount() == 3 );
    CHECK( players.GetPlayerForGamepad( 3 ) == 2 );

    // Unplugging and replugging a pad.
    devices.Gamepads[2].Connected = false;
    frame();
    devices.Gamepads[2].Connected = true;
    frame();
    REQUIRE( log.Events.size() == 5 );
    CHECK( log.Events[3].Type == InputPlayerEvent::Change::DeviceLost );
    CHECK( log.Events[4].Type == InputPlayerEvent::Change::DeviceRegained );
    CHECK( log.Events[4].Player == 1 );

    // Players rebind independently.
    REQUIRE( players.GetActions( 1 ).SetBindingOverride( "Jump", 0, "", "Gamepad/East" ) );
    devices.Gamepads[1].Buttons[south] = true;
    devices.Gamepads[2].Buttons[static_cast<size_t>( GamepadButton::East )] = true;
    frame();
    CHECK( players.GetActions( 0 ).GetAction( "Jump" ).IsPressed() );
    CHECK( players.GetActions( 1 ).GetAction( "Jump" ).IsPressed() );

    // Removing a player moves the later ones down.
    players.RemovePlayer( 1 );
    CHECK( players.GetPlayerCount() == 2 );
    CHECK( players.GetGamepad( 1 ) == 3 );
}
