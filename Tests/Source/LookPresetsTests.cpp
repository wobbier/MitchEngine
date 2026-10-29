#include <doctest/doctest.h>
#include "Components/Camera.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Graphics/PostProcess.h"
#include "Components/Transform.h"
#include "Engine/LookPresets.h"
#include "Engine/World.h"
#include "Graphics/Material.h"
#include "Graphics/Texture.h"
#include "Path.h"

TEST_CASE( "Look presets: parse, replace by name, and the Lite cap layer" )
{
    LookPresets looks;
    REQUIRE( looks.AddPresets( json::parse( R"({ "Presets": [
        { "Name": "Crisp", "Camera": { "RenderScale": 0.5 }, "Post": { "Bloom": false } },
        { "Name": "Tiny", "Lite": true, "Caps": { "MaxTextureSize": 512 }, "Import": { "Format": "None", "MIPs": false } } ] })" ) ) );
    CHECK( looks.GetAll().size() == 2 );
    REQUIRE( looks.Find( "Crisp" ) );
    CHECK( looks.Find( "Crisp" )->Camera["RenderScale"].get<float>() == doctest::Approx( 0.5f ) );
    CHECK_FALSE( looks.AddPresets( json::parse( "[1]" ) ) );
    // The same name replaces.
    REQUIRE( looks.AddPresets( json::parse( R"({ "Presets": [ { "Name": "Crisp", "Description": "again" } ] })" ) ) );
    CHECK( looks.GetAll().size() == 2 );
    CHECK( looks.Find( "Crisp" )->Description == "again" );
    CHECK( looks.Find( "Crisp" )->Camera.empty() );

    const HardwareProfile::FeatureCaps caps = LookPresets::CapsOf( *looks.Find( "Tiny" ) );
    CHECK( caps.MaxTextureSize == 512 );
    CHECK_FALSE( caps.AmbientOcclusion );
    CHECK_FALSE( caps.Shadows );
    CHECK_FALSE( caps.EnvironmentLighting );
    CHECK( caps.Bloom );
}


TEST_CASE( "Look presets: the engine's ten looks load (era effects included)" )
{
    if( !Path( "Engine/Assets/Config/LookPresets.json" ).Exists )
    {
        return;
    }
    LookPresets looks;
    looks.Load();
    for( const char* name : { "Realistic", "Stylized-Lit", "Toon", "Flat/Unlit", "Pixel-2D", "PS1", "N64", "Retro-PC", "Painterly", "VHS" } )
    {
        CHECK_MESSAGE( looks.Find( name ), name );
    }
    REQUIRE( looks.Find( "Flat/Unlit" ) );
    CHECK( looks.Find( "Flat/Unlit" )->Lite );
    CHECK( looks.Find( "Toon" )->Material.is_object() );
    CHECK( looks.Find( "PS1" )->Post["EraAffineTextures"].get<bool>() );
    CHECK( looks.Find( "Painterly" )->Post["EraKuwahara"].get<int>() == 6 );
}


// Mesh materials need the GPU (their uniforms), so the material swap is LookFlows' to check.
TEST_CASE( "Look presets: applying merges onto cameras and adds post-processing" )
{
    LookPresets looks;
    REQUIRE( looks.AddPresets( json::parse( R"({ "Presets": [ { "Name": "Retro",
        "Camera": { "RenderScale": 0.25, "Upscale": "Nearest", "SunShadows": false },
        "Post": { "Bloom": false, "FXAA": false, "VignetteIntensity": 0.4 },
        "Material": { "Type": "DiffuseMaterial", "DiffuseColor": [ 1.0, 0.0, 0.0 ] } } ] })" ) ) );
    const LookPresets::Preset& retro = *looks.Find( "Retro" );
    World world;
    world.IsLoading = false;
    EntityHandle cameraEntity = world.CreateEntity( "Camera" );
    cameraEntity->AddComponent<Transform>();
    Camera& camera = cameraEntity->AddComponent<Camera>();
    camera.Far = 321.f;
    world.Simulate();

    CHECK( LookPresets::ApplyToWorld( retro, world, true ) == 1 );
    CHECK( camera.RenderScale == doctest::Approx( 0.25f ) );
    CHECK( camera.Upscale == Moonlight::UpscaleFilter::Nearest );
    CHECK_FALSE( camera.SunShadows );
    CHECK( camera.Far == doctest::Approx( 321.f ) );   // untouched fields stay
    PostProcess* post = cameraEntity->TryGetComponent<PostProcess>();
    REQUIRE( post );
    CHECK_FALSE( post->Bloom );
    CHECK( post->VignetteIntensity == doctest::Approx( 0.4f ) );
    // Again: nothing left to change.
    CHECK( LookPresets::ApplyToWorld( retro, world, true ) == 0 );
}


TEST_CASE( "Look presets: project defaults set the texture import defaults" )
{
    LookPresets::Preset pixel;
    pixel.Import = json{ { "Format", "None" }, { "MIPs", false } };
    LookPresets::ApplyProjectDefaults( &pixel );
    TextureResourceMetadata fresh( Path( "Assets/NoSuchImage.png" ) );
    CHECK( fresh.OutputFormat == TextureResourceMetadata::OutputTextureFormat::None );
    CHECK_FALSE( fresh.GenerateMIPs );
    LookPresets::ApplyProjectDefaults( nullptr );
    TextureResourceMetadata stock( Path( "Assets/NoSuchImage.png" ) );
    CHECK( stock.OutputFormat == TextureResourceMetadata::OutputTextureFormat::Auto );
    CHECK( stock.GenerateMIPs );
}
