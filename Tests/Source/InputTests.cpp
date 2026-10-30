#include <doctest/doctest.h>
#include "Input/InputPlayers.h"
#include "Engine/Input.h"
#include <SDL.h>
#if defined(__linux__)
#include <cstdlib>
#endif
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

TEST_CASE( "Input: fixed steps see each press and release once, at any frame rate" )
{
    InputActionSystem actions;
    actions.SetMap( GameMap() );
    InputDeviceState devices;
    actions.Update( devices, 0.004f );

    // 240 fps: a tap is pressed and released in frames that run no fixed step; the next step sees both once.
    Key( devices, "Space", true );
    actions.Update( devices, 0.004f );
    Key( devices, "Space", false );
    actions.Update( devices, 0.004f );
    actions.Update( devices, 0.004f );
    actions.BeginFixedStep();
    CHECK( actions.GetAction( "Jump" ).WasPressed() );
    CHECK( actions.GetAction( "Jump" ).WasReleased() );
    CHECK_FALSE( actions.GetAction( "Jump" ).IsPressed() );
    actions.EndFixedStep();
    CHECK_FALSE( actions.GetAction( "Jump" ).WasPressed() );
    actions.BeginFixedStep();
    CHECK_FALSE( actions.GetAction( "Jump" ).WasPressed() );
    actions.EndFixedStep();

    // 20 fps: three steps in the frame of a press; only the first sees it, and the frame keeps its own edge.
    Key( devices, "Space", true );
    actions.Update( devices, 0.05f );
    int seen = 0;
    for( int step = 0; step < 3; ++step )
    {
        actions.BeginFixedStep();
        seen += actions.GetAction( "Jump" ).WasPressed() ? 1 : 0;
        CHECK( actions.GetAction( "Jump" ).IsPressed() );
        actions.EndFixedStep();
    }
    CHECK( seen == 1 );
    CHECK( actions.GetAction( "Jump" ).WasPressed() );

    // Discarded edges (frames that can't step, e.g. while paused) never reach a step.
    Key( devices, "Space", false );
    actions.Update( devices, 0.05f );
    actions.DiscardFixedStepEdges();
    actions.BeginFixedStep();
    CHECK_FALSE( actions.GetAction( "Jump" ).WasReleased() );
    actions.EndFixedStep();
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
            Listen<InputPlayerEvent>( [this]( const InputPlayerEvent& InEvent ) {
                Events.push_back( { InEvent.Player, InEvent.GamepadSlot, InEvent.Type } );
            } );
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

TEST_CASE( "Input: a captured Game View uses relative motion and releases it for the editor" )
{
    // This case needs a display; run via Tools/Headless.sh in CI. Other input tests stay headless.
    if( SDL_InitSubSystem( SDL_INIT_VIDEO ) != 0 )
    {
        WARN( "No SDL video driver available for relative mouse capture test" );
        return;
    }
    SDL_Window* window = SDL_CreateWindow( "Input capture regression", 40, 40, 640, 480, SDL_WINDOW_SHOWN );
    REQUIRE( window != nullptr );
    {
        Input input;
        input.SetCursorRegion( window, Vector2( 80, 60 ), Vector2( 320, 240 ) );
        input.SetMouseCapture( true );
        CHECK( SDL_GetRelativeMouseMode() == SDL_TRUE );
        // Updating the embedded rectangle each frame must not toggle capture or switch to warps.
        input.SetCursorRegion( window, Vector2( 90, 70 ), Vector2( 300, 220 ) );
        CHECK( SDL_GetRelativeMouseMode() == SDL_TRUE );
        input.SetMouseCapture( false );
        CHECK( SDL_GetRelativeMouseMode() == SDL_FALSE );
        input.SetMouseCapture( true );
        CHECK( SDL_GetRelativeMouseMode() == SDL_TRUE );
        input.Pause();
        CHECK( SDL_GetRelativeMouseMode() == SDL_FALSE );
        input.Resume();
        CHECK( SDL_GetRelativeMouseMode() == SDL_TRUE );
        input.Stop();
        input.ClearCursorRegion();
        CHECK( SDL_GetRelativeMouseMode() == SDL_FALSE );
    }
    SDL_DestroyWindow( window );
    SDL_QuitSubSystem( SDL_INIT_VIDEO );
}

#if defined(__linux__)
TEST_CASE( "Input: slow OS mouse motion reaches a captured Game View in all directions" )
{
    const char* libraryPath = std::getenv( "MITCH_TEST_XTEST_LIBRARY" );
    if( !libraryPath ) return; // Opt-in OS injection on a private Xvfb display.
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    SDL_Window* window = SDL_CreateWindow( "Slow mouse regression", 40, 40, 320, 240, SDL_WINDOW_SHOWN );
    REQUIRE( window != nullptr );
    void* library = SDL_LoadObject( libraryPath );
    REQUIRE( library != nullptr );
    using MoveMouse = int ( * )( void*, int, int, unsigned long );
    auto moveMouse = reinterpret_cast<MoveMouse>( SDL_LoadFunction( library, "XTestFakeRelativeMotionEvent" ) );
    REQUIRE( moveMouse != nullptr );
    using Flush = int ( * )( void* );
    void* x11 = SDL_LoadObject( std::getenv( "MITCH_TEST_X11_LIBRARY" ) );
    REQUIRE( x11 != nullptr );
    auto flush = reinterpret_cast<Flush>( SDL_LoadFunction( x11, "XFlush" ) );
    REQUIRE( flush != nullptr );
    using OpenDisplay = void* ( * )( const char* );
    using CloseDisplay = int ( * )( void* );
    auto openDisplay = reinterpret_cast<OpenDisplay>( SDL_LoadFunction( x11, "XOpenDisplay" ) );
    auto closeDisplay = reinterpret_cast<CloseDisplay>( SDL_LoadFunction( x11, "XCloseDisplay" ) );
    REQUIRE( openDisplay != nullptr ); REQUIRE( closeDisplay != nullptr );
    void* display = openDisplay( nullptr );
    REQUIRE( display != nullptr );
    SDL_Window* editorWindow = SDL_CreateWindow( "Editor owning keyboard focus", 500, 40, 320, 240, SDL_WINDOW_SHOWN );
    REQUIRE( editorWindow != nullptr );
    SDL_RaiseWindow( editorWindow );
    SDL_SetWindowInputFocus( editorWindow );
    SDL_WarpMouseInWindow( editorWindow, 160, 120 );
    const auto pump = [] {
        SDL_Delay( 5 );
        SDL_Event event;
        while( SDL_PollEvent( &event ) ) {}
    };
    // A real compositor maps/focuses windows asynchronously; Xvfb does it immediately.
    for( int attempt = 0; attempt < 200 && SDL_GetKeyboardFocus() != editorWindow; ++attempt )
    {
        SDL_RaiseWindow( editorWindow );
        SDL_SetWindowInputFocus( editorWindow );
        pump();
    }
    REQUIRE( SDL_GetKeyboardFocus() == editorWindow );
    SDL_WarpMouseInWindow( editorWindow, 160, 120 );
    pump();
    {
        Input input;
        input.SetCursorRegion( window, Vector2( 200, 150 ), Vector2( 100, 80 ) );
        input.SetMouseCapture( true );
        pump();
        input.Update(); input.PostUpdate();
        for( const Vector2 step : { Vector2( -1, 0 ), Vector2( 1, 0 ), Vector2( 0, -1 ), Vector2( 0, 1 ) } )
        {
            Vector2 total;
            for( int i = 0; i < 24; ++i )
            {
                REQUIRE( moveMouse( display, static_cast<int>( step.x ), static_cast<int>( step.y ), 0 ) != 0 );
                flush( display );
                pump();
                input.Update();
                total = total + input.GetDeviceState().MouseDelta;
                input.PostUpdate();
            }
            INFO( "focus=" << SDL_GetWindowID( SDL_GetKeyboardFocus() ) << " target=" << SDL_GetWindowID( window ) << " relative=" << SDL_GetRelativeMouseMode() << " warp=" << ( SDL_GetHint( SDL_HINT_MOUSE_RELATIVE_MODE_WARP ) ? SDL_GetHint( SDL_HINT_MOUSE_RELATIVE_MODE_WARP ) : "default" ) );
            INFO( "OS step: " << step.x << ", " << step.y << "; received: " << total.x << ", " << total.y );
            CHECK( total.x == doctest::Approx( step.x * 24 ) );
            CHECK( total.y == doctest::Approx( -step.y * 24 ) );
        }
        input.Stop();
        input.ClearCursorRegion();
    }
    closeDisplay( display );
    SDL_DestroyWindow( editorWindow );
    SDL_UnloadObject( x11 ); SDL_UnloadObject( library );
    SDL_DestroyWindow( window );
    SDL_QuitSubSystem( SDL_INIT_VIDEO );
}
#endif
