#include "PCH.h"
#include <doctest/doctest.h>
#include "Engine/DevConsole.h"

// PLAY-10: the console's parsing, commands, variables and completion (C# commands: ConsoleFlows).
TEST_CASE( "Console: lines split, commands run, variables read and set, names complete" )
{
    CHECK( DevConsole::Split( "  spawn \"Assets/My Crate.prefab\"  3 " ) == std::vector<std::string>{ "spawn", "Assets/My Crate.prefab", "3" } );
    CHECK( DevConsole::Split( "" ).empty() );

    DevConsole& console = DevConsole::Get();
    std::vector<std::string> seen;
    console.Register( "test-collect", "Remembers its arguments", [&seen]( const std::vector<std::string>& InArgs ) {
        seen = InArgs;
        return std::string( "got " ) + std::to_string( InArgs.size() );
    } );
    CHECK( console.Execute( "test-collect a \"b c\"" ) == "got 2" );
    CHECK( seen == std::vector<std::string>{ "a", "b c" } );
    CHECK( console.GetLastOutput() == "got 2" );
    CHECK( console.GetHistory().back() == "got 2" );

    float value = 1.f;
    console.RegisterVariable( "test-volume", "A number", [&value]() { return std::to_string( value ); },
        [&value]( const std::string& InText ) { value = std::stof( InText ); return true; } );
    CHECK( console.Execute( "test-volume 0.25" ).rfind( "test-volume = 0.25", 0 ) == 0 );
    CHECK( value == doctest::Approx( 0.25f ) );

    CHECK( console.Execute( "echo hi there" ) == "hi there" );
    CHECK( console.Execute( "no-such-thing" ).find( "Unknown command" ) != std::string::npos );
    CHECK( console.Execute( "help test-" ).find( "test-collect - Remembers its arguments" ) != std::string::npos );
    const std::vector<std::string> matches = console.Complete( "test-" );
    CHECK( matches == std::vector<std::string>{ "test-collect", "test-volume" } );
    console.Execute( "clear" );
    CHECK( console.GetHistory().size() <= 2 );
}
