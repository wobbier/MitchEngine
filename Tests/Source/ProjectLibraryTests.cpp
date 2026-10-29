#include <doctest/doctest.h>
#include "Engine/Project.h"
#include "JSON.h"
#include "Tools/Projects/ProjectLibrary.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ProjectLibraryTest
{
    namespace fs = std::filesystem;

    struct TempDir
    {
        explicit TempDir( const char* InRelative )
        {
            Absolute = fs::absolute( InRelative ).lexically_normal().generic_string();
            fs::remove_all( Absolute );
            fs::create_directories( Absolute );
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all( Absolute, ec );
        }

        std::string Absolute;
    };


    void WriteTextFile( const std::string& InPath, const std::string& InText )
    {
        fs::create_directories( fs::path( InPath ).parent_path() );
        std::ofstream stream( InPath );
        stream << InText;
    }


    json ReadJson( const std::string& InPath )
    {
        std::ifstream stream( InPath );
        std::stringstream text;
        text << stream.rdbuf();
        return json::parse( text.str(), nullptr, false );
    }


    // A template folder under <engine>/Templates.
    std::string MakeTemplate( const std::string& InEngine, const std::string& InId, const std::string& InDescription )
    {
        const std::string folder = InEngine + "/Templates/" + InId;
        WriteTextFile( folder + "/Template.json", InDescription );
        WriteTextFile( folder + "/Project/Game.meproj", "{\"ProjectName\":\"" + InId + "\",\"Keep\":true}" );
        WriteTextFile( folder + "/Assets/Config/ProjectSettings.json", "{\"Product\":{\"Name\":\"" + InId + "\",\"StartupScene\":\"Assets/Scenes/Main.lvl\"}}" );
        WriteTextFile( folder + "/Assets/Config/Engine.cfg", "{\"Title\":\"" + InId + "\"}" );
        WriteTextFile( folder + "/Assets/Scenes/Main.lvl", "{\"Scene\":[]}" );
        WriteTextFile( folder + "/.gitignore", ".tmp/\n" );
        return folder;
    }
}

using namespace ProjectLibraryTest;


TEST_CASE( "Project Hub: project names double as folder names" )
{
    std::string error;
    CHECK( ProjectLibrary::IsValidProjectName( "NewGame", error ) );
    CHECK( ProjectLibrary::IsValidProjectName( "my_game-2", error ) );
    CHECK( ProjectLibrary::IsValidProjectName( "7Seas", error ) );
    CHECK_FALSE( ProjectLibrary::IsValidProjectName( "", error ) );
    CHECK_FALSE( ProjectLibrary::IsValidProjectName( "-dash", error ) );
    CHECK_FALSE( ProjectLibrary::IsValidProjectName( "My Game", error ) );
    CHECK_FALSE( ProjectLibrary::IsValidProjectName( "../escape", error ) );
    CHECK_FALSE( ProjectLibrary::IsValidProjectName( "a/b", error ) );
    CHECK_FALSE( ProjectLibrary::IsValidProjectName( std::string( 65, 'a' ), error ) );
    CHECK_FALSE( error.empty() );
}


TEST_CASE( "Project Hub: templates are the engine's Templates folders, in order" )
{
    TempDir engine( ".tmp/Tests/ProjectLibrary/Engine" );
    MakeTemplate( engine.Absolute, "Second", "{\"Name\":\"Third Person\",\"Description\":\"A character.\",\"Order\":2}" );
    MakeTemplate( engine.Absolute, "First", "{\"Name\":\"Empty\",\"Order\":0}" );
    // Not templates: no Template.json, and a Template.json without a project.
    WriteTextFile( engine.Absolute + "/Templates/Loose/Project/Game.meproj", "{}" );
    WriteTextFile( engine.Absolute + "/Templates/NoProject/Template.json", "{\"Name\":\"Broken\"}" );

    const std::vector<ProjectLibrary::Template> templates = ProjectLibrary::ListTemplates( engine.Absolute );
    REQUIRE( templates.size() == 2 );
    CHECK( templates[0].Id == "First" );
    CHECK( templates[0].Name == "Empty" );
    CHECK( templates[1].Name == "Third Person" );
    CHECK( templates[1].Description == "A character." );
    CHECK( templates[1].Directory == engine.Absolute + "/Templates/Second" );
    CHECK( ProjectLibrary::FindTemplate( templates, "third person" ) == &templates[1] );
    CHECK( ProjectLibrary::FindTemplate( templates, "first" ) == &templates[0] );
    CHECK( ProjectLibrary::FindTemplate( templates, "Missing" ) == nullptr );
}


TEST_CASE( "Project Hub: a new project is a named copy of its template" )
{
    TempDir engine( ".tmp/Tests/ProjectLibrary/Engine" );
    TempDir projects( ".tmp/Tests/ProjectLibrary/Projects" );
    const std::string source = MakeTemplate( engine.Absolute, "Empty", "{\"Name\":\"Empty\"}" );
    // Derived folders never travel with a template.
    WriteTextFile( source + "/.tmp/Scripts/Game.Script.dll", "stale" );
    WriteTextFile( source + "/Project/obj/project.assets.json", "{}" );
    const std::vector<ProjectLibrary::Template> templates = ProjectLibrary::ListTemplates( engine.Absolute );
    REQUIRE( templates.size() == 1 );

    std::string error;
    const std::string root = ProjectLibrary::CreateProject( templates[0], projects.Absolute, "NewGame", error );
    REQUIRE_MESSAGE( !root.empty(), error );
    CHECK( root == projects.Absolute + "/NewGame" );
    CHECK( Project::FindProjectRoot( root ) == root );
    CHECK( fs::is_regular_file( root + "/Assets/Scenes/Main.lvl" ) );
    CHECK( fs::is_regular_file( root + "/.gitignore" ) );
    CHECK_FALSE( fs::exists( root + "/Template.json" ) );
    CHECK_FALSE( fs::exists( root + "/.tmp" ) );
    CHECK_FALSE( fs::exists( root + "/Project/obj" ) );

    const json project = ReadJson( root + "/Project/Game.meproj" );
    CHECK( project["ProjectName"] == "NewGame" );
    CHECK( project["Keep"] == true );
    CHECK( ReadJson( root + "/Assets/Config/ProjectSettings.json" )["Product"]["Name"] == "NewGame" );
    CHECK( ReadJson( root + "/Assets/Config/ProjectSettings.json" )["Product"]["StartupScene"] == "Assets/Scenes/Main.lvl" );
    CHECK( ReadJson( root + "/Assets/Config/Engine.cfg" )["Title"] == "NewGame" );
    CHECK( ProjectLibrary::ReadProjectName( root ) == "NewGame" );

    SUBCASE( "an existing project is never overwritten" )
    {
        CHECK( ProjectLibrary::CreateProject( templates[0], projects.Absolute, "NewGame", error ).empty() );
        CHECK( error.find( "already exists" ) != std::string::npos );
        CHECK( fs::is_regular_file( root + "/Assets/Scenes/Main.lvl" ) );
    }
    SUBCASE( "an empty folder can take the project" )
    {
        fs::create_directories( projects.Absolute + "/Ready" );
        CHECK_FALSE( ProjectLibrary::CreateProject( templates[0], projects.Absolute, "Ready", error ).empty() );
    }
    SUBCASE( "bad names and locations create nothing" )
    {
        CHECK( ProjectLibrary::CreateProject( templates[0], projects.Absolute, "Bad Name", error ).empty() );
        CHECK_FALSE( fs::exists( projects.Absolute + "/Bad Name" ) );
        CHECK( ProjectLibrary::CreateProject( templates[0], "", "Fine", error ).empty() );
        CHECK( ProjectLibrary::CreateProject( templates[0], source, "Inside", error ).empty() );
        CHECK_FALSE( fs::exists( source + "/Inside" ) );
    }
}


TEST_CASE( "Project Hub: the recent list keeps the latest first and survives a reload" )
{
    TempDir projects( ".tmp/Tests/ProjectLibrary/Recent" );
    WriteTextFile( projects.Absolute + "/A/Project/Game.meproj", "{\"ProjectName\":\"Alpha\"}" );
    WriteTextFile( projects.Absolute + "/B/Project/Game.meproj", "{}" );
    const std::string file = projects.Absolute + "/User/RecentProjects.json";

    ProjectLibrary::RecentProjects recent( file );
    recent.Load();
    CHECK( recent.GetEntries().empty() );
    recent.Add( projects.Absolute + "/A" );
    recent.Add( projects.Absolute + "/B/" );
    recent.Add( projects.Absolute + "/Gone" );
    recent.Add( projects.Absolute + "/A" );
    REQUIRE( recent.GetEntries().size() == 3 );
    CHECK( recent.GetEntries()[0].Root == projects.Absolute + "/A" );
    CHECK( recent.GetEntries()[0].Name == "Alpha" );
    CHECK( recent.GetEntries()[0].LastOpened > 0 );
    // No ProjectName: the folder's name.
    CHECK( recent.GetEntries()[2].Name == "B" );
    CHECK( recent.Save() );

    ProjectLibrary::RecentProjects reloaded( file );
    reloaded.Load();
    REQUIRE( reloaded.GetEntries().size() == 3 );
    CHECK( reloaded.GetEntries()[1].Root == projects.Absolute + "/Gone" );
    CHECK( reloaded.Contains( projects.Absolute + "/B" ) );
    CHECK( reloaded.Remove( projects.Absolute + "/Gone" ) );
    CHECK_FALSE( reloaded.Remove( projects.Absolute + "/Gone" ) );
    CHECK( reloaded.GetEntries().size() == 2 );

    for( size_t i = 0; i < ProjectLibrary::RecentProjects::kMaxEntries + 5; ++i )
    {
        reloaded.Add( projects.Absolute + "/Many" + std::to_string( i ) );
    }
    CHECK( reloaded.GetEntries().size() == ProjectLibrary::RecentProjects::kMaxEntries );
    CHECK( reloaded.GetEntries()[0].Root == projects.Absolute + "/Many" + std::to_string( ProjectLibrary::RecentProjects::kMaxEntries + 4 ) );
}


TEST_CASE( "Project Hub: the engine ships the Empty template as a C#-only project" )
{
    const std::string engine = fs::absolute( "Engine" ).lexically_normal().generic_string();
    if( !fs::is_directory( engine + "/Templates" ) )
    {
        MESSAGE( "Skipped: run from a project root with the engine at Engine/" );
        return;
    }
    const std::vector<ProjectLibrary::Template> templates = ProjectLibrary::ListTemplates( engine );
    const ProjectLibrary::Template* empty = ProjectLibrary::FindTemplate( templates, "Empty" );
    REQUIRE( empty != nullptr );
    CHECK( fs::is_regular_file( empty->Directory + "/Project/Game.Script.csproj" ) );
    CHECK_FALSE( fs::exists( empty->Directory + "/Game.sharpmake.cs" ) );
    const json settings = ReadJson( empty->Directory + "/Assets/Config/ProjectSettings.json" );
    REQUIRE( settings.is_object() );
    CHECK( fs::is_regular_file( empty->Directory + "/" + settings["Product"]["StartupScene"].get<std::string>() ) );
}


TEST_CASE( "Project Hub: every engine template is a complete C#-only project" )
{
    const std::string engine = fs::absolute( "Engine" ).lexically_normal().generic_string();
    if( !fs::is_directory( engine + "/Templates" ) )
    {
        MESSAGE( "Skipped: run from a project root with the engine at Engine/" );
        return;
    }
    auto readText = []( const std::string& InPath ) {
        std::ifstream stream( InPath, std::ios::binary );
        std::stringstream text;
        text << stream.rdbuf();
        return text.str();
    };
    const std::vector<ProjectLibrary::Template> templates = ProjectLibrary::ListTemplates( engine );
    const ProjectLibrary::Template* empty = ProjectLibrary::FindTemplate( templates, "Empty" );
    REQUIRE( empty != nullptr );
    for( const char* id : { "FirstPerson", "ThirdPerson", "TopDown" } )
    {
        CHECK_MESSAGE( ProjectLibrary::FindTemplate( templates, id ) != nullptr, id );
    }
    // Every template is a full project folder (openable on its own to edit it), so the files they
    // all share are kept identical to Empty's: the scripts project, the ignore list, the input map,
    // the default editor layout and the pause menu script.
    for( const ProjectLibrary::Template& entry : templates )
    {
        INFO( entry.Id );
        for( const char* shared : { "Project/Game.Script.csproj", ".gitignore", "Assets/Config/Input.inputactions", "Assets/Config/imgui.cfg", "Assets/Scripts/PauseMenu.cs" } )
        {
            INFO( shared );
            REQUIRE( fs::is_regular_file( entry.Directory + "/" + shared ) );
            CHECK( readText( entry.Directory + "/" + shared ) == readText( empty->Directory + "/" + shared ) );
        }
        const json project = ReadJson( entry.Directory + "/Project/Game.meproj" );
        REQUIRE( project.is_object() );
        CHECK( project.value( "ProjectName", std::string() ) == entry.Id );
        const json settings = ReadJson( entry.Directory + "/Assets/Config/ProjectSettings.json" );
        REQUIRE( settings.is_object() );
        CHECK( fs::is_regular_file( entry.Directory + "/" + settings["Product"]["StartupScene"].get<std::string>() ) );
        CHECK( ReadJson( entry.Directory + "/Assets/Config/Input.inputactions" ).is_object() );
    }
}
