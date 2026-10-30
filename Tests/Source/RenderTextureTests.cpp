#include <doctest/doctest.h>
#include "Components/Camera.h"
#include "Device/FrameBuffer.h"
#include "Utils/BGFXUtils.h"
#include "Graphics/RenderTextureSettings.h"
#include "Graphics/Texture.h"
#include "Materials/StandardMaterial.h"
#include "Tools/Cook/Cooker.h"
#include "VFS/VFS.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace RenderTextureTest
{
    // A scratch asset root mounted over the project (logical "Assets/..." paths resolve into it).
    struct TempRoot
    {
        explicit TempRoot( const char* InRelative )
        {
            Root = std::filesystem::absolute( InRelative ).lexically_normal().generic_string();
            std::filesystem::remove_all( Root );
            std::filesystem::create_directories( Root );
            VFS::Get().MountDirectory( "", Root, 300 );
        }

        ~TempRoot()
        {
            VFS::Get().ResetToDefaults();
            std::error_code ec;
            std::filesystem::remove_all( Root, ec );
        }

        void Write( const std::string& InRelative, const std::string& InText ) const
        {
            const std::filesystem::path path = Root + "/" + InRelative;
            std::filesystem::create_directories( path.parent_path() );
            std::ofstream( path ) << InText;
        }

        std::string Root;
    };


    // Restores the current camera a test changes.
    struct CurrentCameraScope
    {
        CurrentCameraScope()
            : Previous( Camera::CurrentCamera )
        {
            Camera::CurrentCamera = nullptr;
        }

        ~CurrentCameraScope()
        {
            Camera::CurrentCamera = Previous;
        }

        Camera* Previous = nullptr;
    };
}


TEST_CASE( "Render textures: settings parse with defaults, clamp and round-trip" )
{
    using Moonlight::RenderTextureSettings;
    using Moonlight::RenderTextureWrap;
    RenderTextureSettings settings;
    CHECK( settings.Width == 640 );
    CHECK( settings.Height == 360 );
    CHECK( settings.Mips );
    CHECK( settings.Wrap == RenderTextureWrap::Clamp );

    REQUIRE( settings.Parse( R"({ "Width": 384, "Height": 200, "Mips": false, "Wrap": "Repeat" })" ) );
    CHECK( settings.Width == 384 );
    CHECK( settings.Height == 200 );
    CHECK_FALSE( settings.Mips );
    CHECK( settings.Wrap == RenderTextureWrap::Repeat );

    // Missing keys go back to their defaults; sizes stay within what a GPU target can be.
    REQUIRE( settings.Parse( R"({ "Width": 0, "Height": 100000 })" ) );
    CHECK( settings.Width == 1 );
    CHECK( settings.Height == RenderTextureSettings::kMaxSize );
    CHECK( settings.Mips );
    CHECK( settings.Wrap == RenderTextureWrap::Clamp );

    CHECK_FALSE( settings.Parse( "[ 640, 360 ]" ) );
    CHECK_FALSE( settings.Parse( "{ not json" ) );

    RenderTextureSettings written;
    written.Width = 256;
    written.Height = 128;
    written.Mips = false;
    written.Wrap = RenderTextureWrap::Mirror;
    RenderTextureSettings read;
    REQUIRE( read.Parse( written.Write() ) );
    CHECK( read == written );
    read.Width = 255;
    CHECK( read != written );
}


TEST_CASE( "Render textures: assets are known by their extension" )
{
    using Moonlight::RenderTextureSettings;
    CHECK( RenderTextureSettings::IsRenderTexturePath( "Assets/Textures/Feed.rendertexture" ) );
    CHECK( RenderTextureSettings::IsRenderTexturePath( "Feed.RenderTexture" ) );
    CHECK_FALSE( RenderTextureSettings::IsRenderTexturePath( "Assets/Textures/Feed.png" ) );
    CHECK_FALSE( RenderTextureSettings::IsRenderTexturePath( "Assets/Feeds.rendertexture/Still.png" ) );
    CHECK_FALSE( RenderTextureSettings::IsRenderTexturePath( "rendertexture" ) );
    CHECK_FALSE( RenderTextureSettings::IsRenderTexturePath( "" ) );
}


TEST_CASE( "Render textures: load without a GPU and keep their target unless the settings change" )
{
    RenderTextureTest::TempRoot root( ".tmp/Tests/RenderTextures" );
    const char* asset = "Assets/RenderTextureTest/Feed.rendertexture";
    root.Write( asset, R"({ "Width": 320, "Height": 180 })" );

    Moonlight::Texture texture{ Path( asset ) };
    REQUIRE( texture.Load() );
    CHECK( texture.IsRenderTarget() );
    CHECK( texture.mWidth == 320 );
    CHECK( texture.mHeight == 180 );
    CHECK( texture.GetRenderTextureSettings().Mips );
    CHECK( texture.GetRenderCount() == 0 );
    const uint32_t revision = texture.GetRenderTargetRevision();
    CHECK( revision > 0 );

    // Saved unchanged: the target, and the handles materials bind, stay.
    texture.Reload();
    CHECK( texture.GetRenderTargetRevision() == revision );

    root.Write( asset, R"({ "Width": 64, "Height": 32, "Mips": false })" );
    texture.Reload();
    CHECK( texture.GetRenderTargetRevision() == revision + 1 );
    CHECK( texture.mWidth == 64 );
    CHECK( texture.mHeight == 32 );

    // A broken save keeps the last good target.
    root.Write( asset, "{ broken" );
    texture.Reload();
    CHECK( texture.GetRenderTargetRevision() == revision + 1 );
    CHECK( texture.mWidth == 64 );

    // A file that failed its first load becomes a render texture once it's fixed.
    const char* late = "Assets/RenderTextureTest/Late.rendertexture";
    root.Write( late, "{ not yet" );
    Moonlight::Texture fixed{ Path( late ) };
    CHECK_FALSE( fixed.Load() );
    CHECK_FALSE( fixed.IsRenderTarget() );
    root.Write( late, R"({ "Width": 16, "Height": 16 })" );
    fixed.Reload();
    CHECK( fixed.IsRenderTarget() );
    CHECK( fixed.mWidth == 16 );

    // Renders are counted for automation and the inspector.
    texture.MarkRendered( 7 );
    CHECK( texture.GetRenderCount() == 1 );
    CHECK( texture.GetLastRenderedFrame() == 7 );
}


TEST_CASE( "Render textures: a camera's link and pass switches round-trip" )
{
    RenderTextureTest::CurrentCameraScope scope;
    Camera feed;
    feed.Deserialize( json{ { "TargetTexture", "Assets/Textures/Feed.rendertexture" }, { "UpdateInterval", 3 },
        { "SunShadows", false }, { "AmbientOcclusion", false }, { "VolumetricFog", false } } );
    CHECK( feed.TargetTexture.GetLocalPathString() == "Assets/Textures/Feed.rendertexture" );
    CHECK( feed.UpdateInterval == 3 );
    CHECK_FALSE( feed.SunShadows );
    CHECK_FALSE( feed.AmbientOcclusion );
    CHECK_FALSE( feed.VolumetricFog );

    json saved;
    feed.Serialize( saved );
    CHECK( saved["TargetTexture"] == "Assets/Textures/Feed.rendertexture" );
    CHECK( saved["UpdateInterval"] == 3 );
    CHECK( saved["SunShadows"] == false );
    Camera restored;
    restored.Deserialize( saved );
    CHECK( restored.TargetTexture.GetLocalPathString() == "Assets/Textures/Feed.rendertexture" );
    CHECK( restored.UpdateInterval == 3 );
    CHECK_FALSE( restored.VolumetricFog );

    // Every frame at most; the pass switches default on, so existing scenes render as before.
    restored.Deserialize( json{ { "UpdateInterval", 0 } } );
    CHECK( restored.UpdateInterval == 1 );
    Camera plain;
    CHECK( plain.SunShadows );
    CHECK( plain.AmbientOcclusion );
    CHECK( plain.VolumetricFog );
    CHECK_FALSE( plain.HasTargetTexture() );
}


TEST_CASE( "Render textures: a feed camera fills its target and never takes the screen" )
{
    RenderTextureTest::CurrentCameraScope scope;
    Camera feed;
    feed.TargetTexture = Path( "Assets/Textures/Feed.rendertexture" );
    feed.Init();
    CHECK( Camera::CurrentCamera != &feed );
    CHECK( feed.RendersToTexture() );

    // A split-screen rect means nothing for a texture: the camera renders the whole target.
    feed.Viewport = Vector4( 0.5f, 0.f, 0.5f, 1.f );
    feed.OutputSize = Vector2( 384.f, 200.f );
    CHECK_FALSE( feed.HasPartialViewport() );
    CHECK( feed.GetViewportSize().x == doctest::Approx( 384.f ) );
    CHECK( feed.GetViewportSize().y == doctest::Approx( 200.f ) );
    CHECK( feed.GetAspectRatio() == doctest::Approx( 1.92f ) );

    // The main camera renders to the screen, even with a target set.
    Camera main;
    main.Init();
    CHECK( Camera::CurrentCamera == &main );
    main.TargetTexture = Path( "Assets/Textures/Feed.rendertexture" );
    CHECK( main.HasTargetTexture() );
    CHECK_FALSE( main.RendersToTexture() );
    Camera::CurrentCamera = nullptr;
}


TEST_CASE( "Materials: a UV offset round-trips and keeps instance batches apart" )
{
    StandardMaterial flipped;
    flipped.Tiling = Vector2( 1.f, -1.f );
    flipped.Offset = Vector2( 0.f, 1.f );
    json saved;
    flipped.OnSerialize( saved );
    REQUIRE( saved.contains( "Offset" ) );
    StandardMaterial restored;
    restored.OnDeserialize( saved );
    CHECK( restored.Offset.x == doctest::Approx( 0.f ) );
    CHECK( restored.Offset.y == doctest::Approx( 1.f ) );
    CHECK( restored.Tiling.y == doctest::Approx( -1.f ) );

    // No offset, no key: existing scenes and materials save as they did.
    StandardMaterial plain;
    json plainSaved;
    plain.OnSerialize( plainSaved );
    CHECK_FALSE( plainSaved.contains( "Offset" ) );

    StandardMaterial shifted;
    shifted.Tiling = Vector2( 1.f, -1.f );
    CHECK( shifted.GetInstanceBatchKey() != flipped.GetInstanceBatchKey() );
    shifted.Offset = flipped.Offset;
    CHECK( shifted.GetInstanceBatchKey() == flipped.GetInstanceBatchKey() );
}


TEST_CASE( "cook: render textures ship as they are, found through cameras and material slots" )
{
    CHECK( Cook::Classify( "Assets/Textures/Feed.rendertexture" ) == Cook::AssetKind::Data );
    CHECK( Cook::ShippedFiles( "Assets/Textures/Feed.rendertexture", Cook::Platform::Linux ) == std::vector<std::string>{ "Assets/Textures/Feed.rendertexture" } );
    CHECK( Cook::OwningAsset( "Assets/Textures/Feed.rendertexture.meta" ) == "Assets/Textures/Feed.rendertexture" );

    RenderTextureTest::TempRoot root( ".tmp/Tests/RenderTextureCook" );
    root.Write( "Assets/RenderTextureCook/Scene.lvl", R"({ "Entities": [
        { "Components": [ { "Type": "Camera", "TargetTexture": "Assets/RenderTextureCook/Feed.rendertexture" } ] },
        { "Components": [ { "Type": "Mesh", "Material": { "Textures": { "Emissive": { "Path": "Assets/RenderTextureCook/Wall.rendertexture" } } } } ] },
        { "Components": [ { "Type": "Mesh", "Material": { "Type": "ShaderGraphMaterial", "Textures": { "Feed": "Assets/RenderTextureCook/Graph.rendertexture" } } } ] } ] })" );
    for( const char* file : { "Assets/RenderTextureCook/Feed.rendertexture", "Assets/RenderTextureCook/Wall.rendertexture", "Assets/RenderTextureCook/Graph.rendertexture" } )
    {
        root.Write( file, R"({ "Width": 64, "Height": 64 })" );
    }
    std::set<std::string> assets;
    Cook::CollectStaticDependencies( "Assets/RenderTextureCook/Scene.lvl", assets );
    CHECK( assets.count( "Assets/RenderTextureCook/Feed.rendertexture" ) == 1 );
    CHECK( assets.count( "Assets/RenderTextureCook/Wall.rendertexture" ) == 1 );
    CHECK( assets.count( "Assets/RenderTextureCook/Graph.rendertexture" ) == 1 );
}


TEST_CASE( "Framebuffers: exhausted bloom allocation keeps the camera renderable" )
{
    bgfx::renderFrame();
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    REQUIRE( bgfx::init( init ) );
    Moonlight::SetGpuAlive( true );
    struct Shutdown
    {
        ~Shutdown() { Moonlight::SetGpuAlive( false ); bgfx::shutdown(); }
    } shutdown;
    {
        Moonlight::FrameBuffer camera( 512, 512 );
        camera.ReCreate( 0 );
        REQUIRE_FALSE( camera.IsIncomplete() );
        const auto texture = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT );
        std::vector<bgfx::FrameBufferHandle> occupied;
        const auto limit = bgfx::getCaps()->limits.maxFrameBuffers;
        for( uint32_t i = 0; i < limit; ++i )
        {
            const auto handle = bgfx::createFrameBuffer( 1, &texture, false );
            if( !bgfx::isValid( handle ) ) break;
            occupied.push_back( handle );
        }
        REQUIRE( occupied.size() >= 2 );
        for( int i = 0; i < 2; ++i )
        {
            bgfx::destroy( occupied.back() );
            occupied.pop_back();
        }
        // bgfx retires destroyed handles after the in-flight frames finish.
        for( int i = 0; i < 3; ++i ) bgfx::frame();
        camera.EnsureBloomTargets();
        CHECK( camera.BloomMips.size() == 2 );
        for( const auto& mip : camera.BloomMips )
        {
            CHECK( bgfx::isValid( mip.Buffer ) );
            CHECK( bgfx::isValid( mip.Color ) );
        }
        CHECK_FALSE( camera.IsIncomplete() );
        for( auto handle : occupied ) bgfx::destroy( handle );
        bgfx::destroy( texture );
    }
}
