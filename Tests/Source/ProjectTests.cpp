#include <doctest/doctest.h>
#include "Engine/Project.h"
#include "Path.h"
#include "VFS/VFS.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ProjectTest
{
    struct TempDir
    {
        explicit TempDir( const char* InRelative )
        {
            Absolute = std::filesystem::absolute( InRelative ).lexically_normal().generic_string();
            std::filesystem::remove_all( Absolute );
            std::filesystem::create_directories( Absolute );
        }

        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all( Absolute, ec );
        }

        std::string Absolute;
    };


    void WriteTextFile( const std::string& InPath, const std::string& InText )
    {
        std::filesystem::create_directories( std::filesystem::path( InPath ).parent_path() );
        std::ofstream stream( InPath );
        stream << InText;
    }


    // Points the default mounts at a project and an engine for the test, then back.
    struct ScopedRoots
    {
        ScopedRoots( const std::string& InGame, const std::string& InEngine )
            : m_game( VFS::Get().GetGameRoot() )
            , m_engine( VFS::Get().GetEngineRoot() )
        {
            VFS::Get().SetDefaultRoots( InGame, InEngine );
        }

        ~ScopedRoots()
        {
            VFS::Get().SetDefaultRoots( m_game, m_engine );
        }

        std::string m_game;
        std::string m_engine;
    };
}


TEST_CASE( "Project: a project is a folder with Project/Game.meproj, named by the folder or its .meproj" )
{
    using namespace ProjectTest;
    TempDir dir( ".tmp/Tests/ProjectRoot" );
    WriteTextFile( dir.Absolute + "/Game/Project/Game.meproj", "{\"ProjectName\":\"Game\"}" );
    WriteTextFile( dir.Absolute + "/Game/Assets/Scenes/Main.lvl", "{}" );

    const std::string root = dir.Absolute + "/Game";
    CHECK( Project::FindProjectRoot( root ) == root );
    CHECK( Project::FindProjectRoot( root + "/" ) == root );
    CHECK( Project::FindProjectRoot( root + "/Project/Game.meproj" ) == root );
    CHECK( Project::FindProjectRoot( ".tmp/Tests/ProjectRoot/Game" ) == root );
    // A folder inside a project, a folder without one, and nothing at all aren't projects.
    CHECK( Project::FindProjectRoot( root + "/Assets" ).empty() );
    CHECK( Project::FindProjectRoot( dir.Absolute ).empty() );
    CHECK( Project::FindProjectRoot( dir.Absolute + "/Missing" ).empty() );
    CHECK( Project::FindProjectRoot( "" ).empty() );
}


TEST_CASE( "Project: the running executable's engine is found above it" )
{
    // The tests run from a game checkout (or the CI's EmptyProject) whose Engine/ sits above
    // .build/, or from an engine checkout, which is its own builds' engine.
    const std::string engine = Project::FindExecutableEngine();
    REQUIRE( !engine.empty() );
    CHECK( Project::IsEngineDirectory( engine ) );
    const std::string root = std::filesystem::current_path().generic_string();
    auto same = []( const std::string& InA, const std::string& InB ) {
        std::error_code error;
        return std::filesystem::equivalent( InA, InB, error );
    };
    CHECK( ( same( engine, root + "/Engine" ) || ( same( engine, root ) && Project::IsEngineCheckout( root ) ) ) );

    ProjectTest::TempDir dir( ".tmp/Tests/ProjectNotEngine" );
    ProjectTest::WriteTextFile( dir.Absolute + "/Assets/Config/Engine.cfg", "{}" );
    CHECK_FALSE( Project::IsEngineDirectory( dir.Absolute ) );
}


TEST_CASE( "VFS: an engine outside the project answers its Engine/ spellings" )
{
    using namespace ProjectTest;
    TempDir dir( ".tmp/Tests/ProjectSharedEngine" );
    const std::string game = dir.Absolute + "/Game";
    const std::string engine = dir.Absolute + "/Install/Engine";
    WriteTextFile( game + "/Project/Game.meproj", "{\"ProjectName\":\"Shared\"}" );
    WriteTextFile( game + "/Assets/Scenes/Main.lvl", "game scene" );
    WriteTextFile( game + "/Assets/Shaders/Override.sh", "game override" );
    WriteTextFile( engine + "/Assets/Config/HardwareProfiles.json", "{}" );
    WriteTextFile( engine + "/Assets/Shaders/Common.sh", "engine common" );
    WriteTextFile( engine + "/Assets/Shaders/Override.sh", "engine original" );
    WriteTextFile( engine + "/Tools/RunTool.sh", "tool" );

    ScopedRoots roots( game, engine );
    CHECK( Project::GetRoot() == game );
    CHECK( Project::GetEngineRoot() == engine );
    CHECK_FALSE( Project::HasEmbeddedEngine() );
    CHECK( Project::GetName() == "Shared" );

    // "Engine/..." names the engine's files, through every Path flavour.
    const Path common( "Engine/Assets/Shaders/Common.sh" );
    CHECK( common.Exists );
    CHECK( common.FullPath == engine + "/Assets/Shaders/Common.sh" );
    CHECK( common.GetLocalPathString() == "Engine/Assets/Shaders/Common.sh" );
    const Path tool( "Engine/Tools/RunTool.sh", true );
    CHECK( tool.Exists );
    CHECK( tool.FullPath == engine + "/Tools/RunTool.sh" );
    CHECK( Path( "Engine/Assets", true ).FullPath == engine + "/Assets" );

    // The overlay still layers the game over the engine.
    CHECK( Path( "Assets/Shaders/Common.sh" ).FullPath == engine + "/Assets/Shaders/Common.sh" );
    CHECK( Path( "Assets/Shaders/Override.sh" ).FullPath == game + "/Assets/Shaders/Override.sh" );
    CHECK( Path( "Assets/Scenes/Main.lvl" ).FullPath == game + "/Assets/Scenes/Main.lvl" );

    std::vector<std::string> listed;
    VFS::Get().Enumerate( "Engine/Assets/Shaders", false, [&listed]( const std::string& InPath, bool ) { listed.push_back( InPath ); } );
    CHECK( listed == std::vector<std::string>{ "Engine/Assets/Shaders/Common.sh", "Engine/Assets/Shaders/Override.sh" } );

    // Reads note the overlay spelling, like an engine read through the project's Engine/ folder.
    VFS::Get().ClearRecordedReads();
    VFS::Get().SetRecordingReads( true );
    std::string text;
    CHECK( VFS::Get().ReadText( std::string( "Engine/Assets/Shaders/Common.sh" ), text ) );
    CHECK( text == "engine common" );
    VFS::Get().SetRecordingReads( false );
    const std::vector<std::string> recorded = VFS::Get().GetRecordedReads();
    CHECK( std::find( recorded.begin(), recorded.end(), "Assets/Shaders/Common.sh" ) != recorded.end() );
    VFS::Get().ClearRecordedReads();

    // Writes to "Engine/..." land in the engine, as they do in a project's own Engine/.
    CHECK( VFS::Get().WriteText( std::string( "Engine/Assets/Written.txt" ), "written" ) );
    CHECK( std::filesystem::is_regular_file( engine + "/Assets/Written.txt" ) );
}


TEST_CASE( "VFS: a project's own Engine/ folder needs no Engine/ mount" )
{
    using namespace ProjectTest;
    TempDir dir( ".tmp/Tests/ProjectEmbeddedEngine" );
    const std::string game = dir.Absolute + "/Game";
    WriteTextFile( game + "/Engine/Assets/Config/HardwareProfiles.json", "{}" );
    WriteTextFile( game + "/Engine/Assets/Shaders/Common.sh", "engine common" );

    ScopedRoots roots( game, game + "/Engine" );
    CHECK( Project::HasEmbeddedEngine() );
    size_t engineMounts = 0;
    for( const VfsMount& mount : VFS::Get().Snapshot()->Mounts )
    {
        engineMounts += mount.Prefix == VFS::kEnginePrefix ? 1 : 0;
    }
    CHECK( engineMounts == 0 );
    // The historical spelling: the game root serves it, displayed from its Assets/ on.
    const Path common( "Engine/Assets/Shaders/Common.sh" );
    CHECK( common.FullPath == game + "/Engine/Assets/Shaders/Common.sh" );
    CHECK( common.GetLocalPathString() == "Assets/Shaders/Common.sh" );
}
