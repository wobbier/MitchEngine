#include "PCH.h"
#include <doctest/doctest.h>
#include "Engine/ControlChannel.h"
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


// NET-7: the PIE control channel (--control-stdin): a line's id and command, and the answer's one line.
TEST_CASE( "Console: control lines carry an id, and answers stay on one line" )
{
    unsigned long long id = 99;
    CHECK( ControlChannel::ParseLine( "12 net field GameMode Deathmatch.ScoreToWin\r", id ) == "net field GameMode Deathmatch.ScoreToWin" );
    CHECK( id == 12 );
    CHECK( ControlChannel::ParseLine( "  7\techo  hi ", id ) == "echo  hi" );
    CHECK( id == 7 );
    CHECK( ControlChannel::ParseLine( "echo hi", id ) == "echo hi" );
    CHECK( id == 0 );
    CHECK( ControlChannel::ParseLine( "3d spawn", id ) == "3d spawn" );
    CHECK( id == 0 );
    CHECK( ControlChannel::ParseLine( "42", id ).empty() );
    CHECK( id == 42 );
    CHECK( ControlChannel::ParseLine( "   ", id ).empty() );
    CHECK( id == 0 );

    const std::string reply = ControlChannel::FormatReply( 5, "line one\nline \"two\"" );
    CHECK( reply == "[control] 5 \"line one\\nline \\\"two\\\"\"" );
    CHECK( reply.find( '\n' ) == std::string::npos );
    // Invalid UTF-8 is replaced, never thrown.
    CHECK( ControlChannel::FormatReply( 0, std::string( "bad \xff byte" ) ).rfind( "[control] 0 \"bad ", 0 ) == 0 );
}
