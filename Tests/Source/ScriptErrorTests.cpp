#include <doctest/doctest.h>
#include "Scripting/ScriptEngine.h"
#include "../../Modules/Havana/Source/Utils/SourceLocation.h"

TEST_CASE( "Scripts: build errors become one log entry each, and errors open their line" )
{
    // dotnet build output: a repeated error, a warning, a Windows path, and noise.
    const std::string output =
        "/home/me/Game/Assets/Scripts/Door.cs(12,5): error CS1002: ; expected [/home/me/Game/Project/Game.Script.csproj]\n"
        "/home/me/Game/Assets/Scripts/Door.cs(12,5): error CS1002: ; expected [/home/me/Game/Project/Game.Script.csproj]\n"
        "/home/me/Game/Assets/Scripts/Key.cs(3,10): warning CS0168: The variable 'x' is declared but never used [/home/me/Game/Project/Game.Script.csproj]\n"
        "C:\\Game\\Assets\\Scripts\\Win.cs(1,1): error CS0246: The type or namespace name 'Foo' could not be found\n"
        "Build FAILED.\n";
    const std::vector<ScriptEngine::BuildDiagnostic> diagnostics = ScriptEngine::ParseBuildDiagnostics( output, "/home/me/Game" );
    REQUIRE( diagnostics.size() == 3 );
    CHECK( diagnostics[0].File == "Assets/Scripts/Door.cs" );
    CHECK( diagnostics[0].Line == 12 );
    CHECK( diagnostics[0].Column == 5 );
    CHECK( diagnostics[0].IsError );
    CHECK( diagnostics[0].Code == "CS1002" );
    CHECK( diagnostics[0].Message == "; expected" );
    CHECK_FALSE( diagnostics[1].IsError );
    CHECK( diagnostics[1].File == "Assets/Scripts/Key.cs" );
    CHECK( diagnostics[2].File == "C:/Game/Assets/Scripts/Win.cs" );
    CHECK( diagnostics[2].Message == "The type or namespace name 'Foo' could not be found" );

    // The Log panel opens the file and line of a logged error, a compiler line or a .NET stack frame.
    std::string file;
    int line = 0;
    CHECK( FindSourceLocation( "Assets/Scripts/Door.cs:12:5: error CS1002: ; expected", file, line ) );
    CHECK( file == "Assets/Scripts/Door.cs" );
    CHECK( line == 12 );
    CHECK( FindSourceLocation( "   at Door.OnUpdate(Single dt) in /home/me/Game/Assets/Scripts/Door.cs:line 42", file, line ) );
    CHECK( file == "/home/me/Game/Assets/Scripts/Door.cs" );
    CHECK( line == 42 );
    CHECK( FindSourceLocation( "Key.cs(7,3): warning CS0168", file, line ) );
    CHECK( line == 7 );
    CHECK_FALSE( FindSourceLocation( "Scripts: hot reloaded", file, line ) );
}
