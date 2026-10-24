#include <doctest/doctest.h>
#include "Engine/ProjectSettings.h"
#include <filesystem>

TEST_CASE( "ProjectSettings: product, startup and build settings round-trip" )
{
    const char* path = ".tmp/ProjectSettingsTests/ProjectSettings.json";
    std::filesystem::remove_all( ".tmp/ProjectSettingsTests" );

    ProjectSettings saved;
    saved.ProductName = "Probe Game";
    saved.ProductOrg = "Probe Org";
    saved.ProductVersion = "1.2.3";
    saved.ProductIcon = "Assets/UI/Drive/Star.png";
    saved.StartupScene = "Assets/Scenes/Tests/UITest.lvl";
    saved.BuildScenes = { "Assets/Scenes/Tests/UITest.lvl", "Assets/SceneGraphTest.lvl" };
    saved.BuildAlwaysInclude = { "Assets/Fonts" };
    saved.BuildExclude = { "Assets/Scenes/Tests" };
    saved.SetLayerName( 3, "Probe Layer" );
    saved.BusVolumes[1] = 0.25f;
    saved.SaveTo( path );

    ProjectSettings loaded;
    loaded.LoadFrom( path );
    CHECK( loaded.ProductName == saved.ProductName );
    CHECK( loaded.ProductOrg == saved.ProductOrg );
    CHECK( loaded.ProductVersion == saved.ProductVersion );
    CHECK( loaded.ProductIcon == saved.ProductIcon );
    CHECK( loaded.StartupScene == saved.StartupScene );
    CHECK( loaded.BuildScenes == saved.BuildScenes );
    CHECK( loaded.BuildAlwaysInclude == saved.BuildAlwaysInclude );
    CHECK( loaded.BuildExclude == saved.BuildExclude );
    CHECK( loaded.GetLayerName( 3 ) == "Probe Layer" );
    CHECK( loaded.BusVolumes[1] == doctest::Approx( 0.25f ) );

    std::error_code ec;
    std::filesystem::remove_all( ".tmp/ProjectSettingsTests", ec );
}


TEST_CASE( "ProjectSettings: a missing file keeps the defaults" )
{
    ProjectSettings fresh;
    fresh.LoadFrom( ".tmp/ProjectSettingsTests/DoesNotExist.json" );
    CHECK( fresh.ProductOrg == "MitchEngine" );
    CHECK( fresh.StartupScene.empty() );
    CHECK( fresh.BuildScenes.empty() );
}
