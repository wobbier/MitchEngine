#include <doctest/doctest.h>
#include "Engine/HardwareBudget.h"
#include "Engine/PlayerSettings.h"
#include "Input/InputActions.h"
#include "JSON.h"
#include "VFS/VFS.h"
#include <filesystem>

namespace PlayerSettingsTests
{
    InputActionMap JumpMap()
    {
        InputBinding key;
        key.Control = "Keyboard/Space";
        InputBinding pad;
        pad.Control = "Gamepad/South";
        InputAction jump;
        jump.Name = "Jump";
        jump.Bindings = { key, pad };
        InputContext context;
        context.Name = "Gameplay";
        context.Actions = { jump };
        InputActionMap map;
        map.Contexts = { context };
        return map;
    }


    // Leaves the shared hardware budget the way a fresh run finds it.
    struct RestoreBudget
    {
        ~RestoreBudget()
        {
            HardwareBudget& budget = HardwareBudget::Get();
            budget.SetQualityPreset( "" );
            budget.SetEnforced( false );
            budget.SetActiveProfile( "" );
            budget.ReloadProfiles();
        }
    };
}

using namespace PlayerSettingsTests;

TEST_CASE( "Player settings: stored documents read over the defaults, keeping only what's valid" )
{
    const json stored = {
        { "Version", 1 },
        { "Graphics", { { "Quality", "Low" } } },
        { "Audio", { { "Music", 0.25 }, { "SFX", 4.0 }, { "Voice", "loud" } } },
        { "Accessibility", { { "TextScale", 0.1 }, { "ReduceMotion", true } } },
        { "Input", { { "Bindings", { { "Gameplay/Jump/0", "Keyboard/J" }, { "Gameplay/Jump/1", "Keyboard/NotAKey" } } } } },
        { "Game", { { "Sensitivity", 1.5 }, { "Nested", { { "Deep", true } } } } },
        { "Telemetry", true },
    };
    std::string notes;
    const json settings = PlayerSettings::Sanitize( stored, &notes );
    CHECK( settings["Graphics"]["Quality"] == "Low" );
    CHECK( settings["Audio"]["Music"].get<double>() == doctest::Approx( 0.25 ) );
    // Out of range clamps; a wrong type keeps the default.
    CHECK( settings["Audio"]["SFX"].get<double>() == doctest::Approx( 1.0 ) );
    CHECK( settings["Audio"]["Voice"].get<double>() == doctest::Approx( 1.0 ) );
    CHECK( settings["Audio"]["Master"].get<double>() == doctest::Approx( 1.0 ) );
    CHECK( settings["Accessibility"]["TextScale"].get<double>() == doctest::Approx( 0.5 ) );
    CHECK( settings["Accessibility"]["ReduceMotion"] == true );
    CHECK( settings["Input"]["Bindings"] == json( { { "Gameplay/Jump/0", "Keyboard/J" } } ) );
    CHECK( settings["Game"]["Nested"]["Deep"] == true );
    CHECK_FALSE( settings.contains( "Telemetry" ) );
    CHECK( settings["Version"] == PlayerSettings::kVersion );
    CHECK( notes.find( "Audio.Voice has the wrong type" ) != std::string::npos );
    CHECK( notes.find( "1 invalid rebinds dropped" ) != std::string::npos );
    CHECK( notes.find( "unknown Telemetry dropped" ) != std::string::npos );

    // A newer file still reads what this build knows.
    std::string newer;
    const json future = PlayerSettings::Sanitize( { { "Version", PlayerSettings::kVersion + 1 }, { "Language", "es" } }, &newer );
    CHECK( future["Language"] == "es" );
    CHECK( newer.find( "newer" ) != std::string::npos );
    CHECK( PlayerSettings::Sanitize( json::array() ) == PlayerSettings::Defaults() );
}


TEST_CASE( "Player settings: values are checked against their key's type and range" )
{
    PlayerSettings settings;
    std::string error;
    CHECK( settings.SetValue( "Audio.Music", 0.5 ) );
    CHECK( settings.GetValue( "Audio.Music" ).get<double>() == doctest::Approx( 0.5 ) );
    CHECK( settings.IsDirty() );
    CHECK( settings.SetValue( "Audio.Music", -2.0 ) );
    CHECK( settings.GetValue( "Audio.Music" ).get<double>() == doctest::Approx( 0.0 ) );
    CHECK_FALSE( settings.SetValue( "Audio.Music", "half", &error ) );
    CHECK( error.find( "takes a number" ) != std::string::npos );
    CHECK_FALSE( settings.SetValue( "Audio.Cowbell", 1.0, &error ) );
    CHECK( error.find( "isn't a setting" ) != std::string::npos );
    CHECK_FALSE( settings.SetValue( "Audio", json::object(), &error ) );
    CHECK( error.find( "is a group" ) != std::string::npos );
    CHECK_FALSE( settings.SetValue( "Version", 2 ) );
    CHECK_FALSE( settings.SetValue( "Accessibility.ReduceMotion", 1 ) );
    CHECK( settings.SetValue( "Accessibility.ReduceMotion", true ) );

    // The game's own values take any JSON, nested by their dots.
    CHECK( settings.SetValue( "Game.Controls.Sensitivity", 2.5 ) );
    CHECK( settings.SetValue( "Game.Name", "Probe" ) );
    CHECK( settings.GetValue( "Game.Controls.Sensitivity" ).get<double>() == doctest::Approx( 2.5 ) );
    CHECK( settings.GetValue( "Game" )["Name"] == "Probe" );
    CHECK_FALSE( settings.SetValue( "Game..Broken", 1 ) );
    CHECK( settings.GetValue( "Game.Missing" ).is_null() );

    settings.Reset();
    CHECK( settings.GetDocument() == PlayerSettings::Defaults() );
}


TEST_CASE( "Player settings: rebinds and values survive a save and a fresh load" )
{
    const std::string path = "user://PlayerSettingsTests/Settings.json";
    const std::filesystem::path physical = VFS::Get().Resolve( path ).PhysicalPath;
    std::filesystem::remove_all( physical.parent_path() );

    InputActionSystem actions;
    actions.SetMap( JumpMap() );
    {
        PlayerSettings settings;
        PlayerSettings::Targets targets;
        targets.Actions = &actions;
        settings.SetTargets( targets );
        CHECK_FALSE( settings.Save() );     // in memory only until it has a path
        settings.SetPath( path );
        REQUIRE( settings.Rebind( "Jump", 0, "", "Keyboard/J" ) );
        CHECK_FALSE( settings.Rebind( "Jump", 0, "", "Keyboard/NotAKey" ) );
        CHECK( settings.GetValue( "Input.Bindings" ) == json( { { "Gameplay/Jump/0", "Keyboard/J" } } ) );
        CHECK( settings.SetValue( "Audio.Voice", 0.75 ) );
        CHECK( settings.SetValue( "Game.Probe", 7 ) );
        REQUIRE( settings.Save() );
        CHECK_FALSE( settings.IsDirty() );
    }
    CHECK( std::filesystem::exists( physical ) );
    // Written through a temporary file that the rename consumed.
    CHECK_FALSE( std::filesystem::exists( physical.string() + ".tmp" ) );

    // A "restart": new input with the default bindings, new settings read from the file.
    InputActionSystem restarted;
    restarted.SetMap( JumpMap() );
    CHECK( restarted.GetBindingDisplay( "Jump", 0 ) == "Space" );
    PlayerSettings loaded;
    PlayerSettings::Targets targets;
    targets.Actions = &restarted;
    loaded.SetTargets( targets );
    loaded.SetPath( path );
    REQUIRE( loaded.Load() );
    CHECK( restarted.GetBindingDisplay( "Jump", 0 ) == "J" );
    CHECK( loaded.GetValue( "Audio.Voice" ).get<double>() == doctest::Approx( 0.75 ) );
    CHECK( loaded.GetValue( "Game.Probe" ) == 7 );

    loaded.ResetBindings();
    CHECK( restarted.GetBindingDisplay( "Jump", 0 ) == "Space" );
    CHECK( loaded.GetValue( "Input.Bindings" ) == json::object() );

    // A broken file reads as the defaults and says so.
    REQUIRE( VFS::Get().WriteText( path, "{ not json" ) );
    CHECK_FALSE( loaded.Load() );
    CHECK( loaded.GetValue( "Game.Probe" ).is_null() );
    std::filesystem::remove_all( physical.parent_path() );
}


TEST_CASE( "Quality presets: data-defined caps, the stricter of them and an enforced profile apply" )
{
    RestoreBudget restore;
    HardwareBudget& budget = HardwareBudget::Get();
    budget.ReloadProfiles();
    // The engine ships Low .. Ultra.
    REQUIRE( budget.FindQualityPreset( "Low" ) != nullptr );
    REQUIRE( budget.FindQualityPreset( "Ultra" ) != nullptr );
    CHECK( budget.GetQualityPresets().front().Name == "Low" );
    CHECK_FALSE( budget.FindQualityPreset( "Low" )->Caps.VolumetricFog );

    REQUIRE( budget.AddProfiles( json::parse( R"({
        "Profiles": [ { "Name": "Probe Target", "Caps": { "MaxTextureSize": 4096, "MaxShadowResolution": 512, "Bloom": false } } ],
        "QualityPresets": [
            { "Name": "Probe Low", "Caps": { "MaxTextureSize": 1024, "MaxShadowResolution": 2048, "AmbientOcclusion": false } },
            { "Name": "Probe Lower", "Extends": "Probe Low", "Caps": { "MaxVolumetricSteps": 8 } }
        ] })" ), "test" ) );
    REQUIRE( budget.FindQualityPreset( "Probe Lower" ) != nullptr );
    CHECK( budget.FindQualityPreset( "Probe Lower" )->Caps.MaxTextureSize == 1024 );
    CHECK( budget.FindQualityPreset( "Probe Lower" )->Caps.MaxVolumetricSteps == 8 );

    // No preset and no enforcement: no caps.
    CHECK( budget.GetEffectiveCaps().MaxTextureSize == 0 );
    CHECK( budget.SetQualityPreset( "Probe Low" ) );
    CHECK( budget.GetEffectiveCaps().MaxTextureSize == 1024 );
    CHECK( budget.GetEffectiveCaps().MaxShadowResolution == 2048 );
    CHECK_FALSE( budget.GetEffectiveCaps().AmbientOcclusion );
    CHECK( budget.GetEffectiveCaps().Bloom );

    // Under an enforced profile each cap takes the stricter side.
    budget.SetActiveProfile( "Probe Target" );
    budget.SetEnforced( true );
    CHECK( budget.GetEffectiveCaps().MaxTextureSize == 1024 );
    CHECK( budget.GetEffectiveCaps().MaxShadowResolution == 512 );
    CHECK_FALSE( budget.GetEffectiveCaps().AmbientOcclusion );
    CHECK_FALSE( budget.GetEffectiveCaps().Bloom );

    // An unknown preset is reported and caps nothing.
    CHECK_FALSE( budget.SetQualityPreset( "Potato" ) );
    CHECK( budget.GetEffectiveCaps().MaxTextureSize == 4096 );

    // Settings drive the preset.
    PlayerSettings settings;
    PlayerSettings::Targets targets;
    targets.Budget = &budget;
    settings.SetTargets( targets );
    CHECK( settings.SetValue( "Graphics.Quality", "Probe Lower" ) );
    CHECK( budget.GetQualityPresetName() == "Probe Lower" );
    CHECK( budget.GetEffectiveCaps().MaxVolumetricSteps == 8 );
}


TEST_CASE( "Player settings: Toggle instead of hold latches Toggle buttons on a press and off on the next (PLAY-7)" )
{
    constexpr size_t kSpace = 44;       // SDL_SCANCODE_SPACE
    constexpr size_t kLeftShift = 225;  // SDL_SCANCODE_LSHIFT
    InputActionMap map = JumpMap();
    InputBinding shift;
    shift.Control = "Keyboard/Left Shift";
    InputAction sprint;
    sprint.Name = "Sprint";
    sprint.Toggle = true;
    sprint.Bindings = { shift };
    map.Contexts[0].Actions.push_back( sprint );
    InputActionSystem actions;
    actions.SetMap( map );
    PlayerSettings settings;
    PlayerSettings::Targets targets;
    targets.Actions = &actions;
    settings.SetTargets( targets );

    InputDeviceState devices;
    auto frame = [&]( size_t InKey, bool InDown ) {
        devices.Keys[InKey] = InDown ? 1 : 0;
        actions.Update( devices, 1.f / 60.f );
    };
    // Off (the default): Sprint is held like any button.
    CHECK_FALSE( actions.GetToggleHolds() );
    frame( kLeftShift, true );
    CHECK( actions.GetAction( "Sprint" ).IsPressed() );
    frame( kLeftShift, false );
    CHECK_FALSE( actions.GetAction( "Sprint" ).IsPressed() );

    // On: a press latches it, the next press lets go; Jump (not Toggle) is still held.
    REQUIRE( settings.SetValue( "Accessibility.ToggleHolds", true ) );
    CHECK( actions.GetToggleHolds() );
    frame( kLeftShift, true );
    CHECK( actions.GetAction( "Sprint" ).WasPressed() );
    frame( kLeftShift, false );
    frame( kLeftShift, false );
    CHECK( actions.GetAction( "Sprint" ).IsPressed() );
    CHECK( actions.GetAction( "Sprint" ).GetValue() == doctest::Approx( 1.f ) );
    CHECK( actions.GetAction( "Sprint" ).HeldSeconds > 0.f );
    frame( kSpace, true );
    frame( kSpace, false );
    CHECK_FALSE( actions.GetAction( "Jump" ).IsPressed() );
    frame( kLeftShift, true );
    CHECK( actions.GetAction( "Sprint" ).WasReleased() );
    frame( kLeftShift, false );
    CHECK_FALSE( actions.GetAction( "Sprint" ).IsPressed() );

    // Latched, then the setting goes off: it lets go at once.
    frame( kLeftShift, true );
    frame( kLeftShift, false );
    REQUIRE( actions.GetAction( "Sprint" ).IsPressed() );
    REQUIRE( settings.SetValue( "Accessibility.ToggleHolds", false ) );
    frame( kLeftShift, false );
    CHECK_FALSE( actions.GetAction( "Sprint" ).IsPressed() );
}
