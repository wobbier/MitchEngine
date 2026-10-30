#include <doctest/doctest.h>
#include "../../Modules/Havana/Source/Utils/ScriptNaming.h"
#include "../../Modules/Havana/Source/Utils/ScriptTemplates.h"
#include <set>

TEST_CASE( "Scripts: new and renamed scripts keep valid, project-wide class names" )
{
    CHECK( ScriptNaming::IsIdentifier( "PlayerController" ) );
    CHECK( ScriptNaming::IsIdentifier( "_Hidden2" ) );
    CHECK_FALSE( ScriptNaming::IsIdentifier( "NewScript 1" ) );
    CHECK_FALSE( ScriptNaming::IsIdentifier( "2DPlayer" ) );
    CHECK_FALSE( ScriptNaming::IsIdentifier( "class" ) );
    CHECK_FALSE( ScriptNaming::IsIdentifier( "" ) );

    // Typed names become the closest identifier.
    CHECK( ScriptNaming::MakeIdentifier( "my cool-script" ) == "MyCoolScript" );
    CHECK( ScriptNaming::MakeIdentifier( "NewScript 1" ) == "NewScript1" );
    CHECK( ScriptNaming::MakeIdentifier( "2D Player" ) == "_2DPlayer" );
    CHECK( ScriptNaming::MakeIdentifier( "event" ) == "Event" );
    CHECK( ScriptNaming::MakeIdentifier( "!!!" ) == "NewScript" );

    // New scripts take the first free name anywhere in the project.
    std::set<std::string> taken = { "NewScript", "NewScript2" };
    CHECK( ScriptNaming::UniqueName( "NewScript", [&]( const std::string& InName ) { return taken.count( InName ) > 0; } ) == "NewScript3" );
    CHECK( ScriptNaming::UniqueName( "Spinner", [&]( const std::string& InName ) { return taken.count( InName ) > 0; } ) == "Spinner" );

    // Renaming the file renames the type declared under the old name, and nothing else.
    std::string source = "public class NewScript : Script\n{\n    NewScriptHelper helper;\n}\n";
    CHECK( ScriptNaming::RenameType( source, "NewScript", "Door" ) );
    CHECK( source == "public class Door : Script\n{\n    NewScriptHelper helper;\n}\n" );
    std::string other = "public class Gate : Script {}";
    CHECK_FALSE( ScriptNaming::RenameType( other, "Door", "Portal" ) );
    CHECK( other == "public class Gate : Script {}" );
    std::string record = "public partial record struct Pickup(int Value);";
    CHECK( ScriptNaming::RenameType( record, "Pickup", "Coin" ) );
    CHECK( record == "public partial record struct Coin(int Value);" );

    // Scenes that still name the old class keep loading it through [FormerName], at the declaration's indent.
    std::string renamed = "using ScriptCore;\n\nnamespace Game\n{\n    public class Door : Script\n    {\n    }\n}\n";
    CHECK( ScriptNaming::AddFormerName( renamed, "Door", "NewScript" ) );
    CHECK( renamed == "using ScriptCore;\n\nnamespace Game\n{\n    [ScriptCore.FormerName(\"NewScript\")]\n    public class Door : Script\n    {\n    }\n}\n" );
    std::string windows = "public class Door : Script\r\n{\r\n}\r\n";
    CHECK( ScriptNaming::AddFormerName( windows, "Door", "Gate" ) );
    CHECK( windows == "[ScriptCore.FormerName(\"Gate\")]\r\npublic class Door : Script\r\n{\r\n}\r\n" );
    CHECK_FALSE( ScriptNaming::AddFormerName( windows, "Missing", "Gate" ) );
}

TEST_CASE( "Scripts: Create > C# Script templates name the class and use the project's namespace (ME-237)" )
{
    CHECK( ScriptTemplates::ProjectNamespace( "MitchGame" ) == "MitchGame" );
    CHECK( ScriptTemplates::ProjectNamespace( "my cool game" ) == "MyCoolGame" );
    CHECK( ScriptTemplates::ProjectNamespace( "2D Shooter" ) == "_2DShooter" );
    CHECK( ScriptTemplates::ProjectNamespace( "" ) == "Game" );
    CHECK( ScriptTemplates::ProjectNamespace( "ScriptCore" ) == "Game" );
    std::set<std::string> labels;
    for( const ScriptTemplates::Template& entry : ScriptTemplates::All() )
    {
        labels.insert( entry.Label );
        CHECK( ScriptNaming::IsIdentifier( entry.BaseName ) );
        const std::string source = ScriptTemplates::Make( entry, "Thing", "MyGame" );
        CHECK( source.find( "namespace MyGame;" ) != std::string::npos );
        CHECK( source.find( "class Thing" ) != std::string::npos );
        CHECK( source.find( "$NAME" ) == std::string::npos );
    }
    CHECK( labels.size() == ScriptTemplates::All().size() );
    REQUIRE( ScriptTemplates::Find( "Networked Script" ) != nullptr );
    CHECK( ScriptTemplates::Make( *ScriptTemplates::Find( "Networked Script" ), "Thing", "" ).find( "partial class Thing" ) != std::string::npos );
    CHECK( ScriptTemplates::Find( "Nope" ) == nullptr );
}
