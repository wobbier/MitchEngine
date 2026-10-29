#include <doctest/doctest.h>
#include "Camera/CameraData.h"
#include "Engine/HardwareBudget.h"
#include "JSON.h"

using Moonlight::UpscaleFilter;

TEST_CASE( "Render scale: internal sizes under scale, caps and the pixel filter" )
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t factor = 0;

    Moonlight::ComputeRenderSize( 1920, 1080, 1.f, UpscaleFilter::Bilinear, 0.f, 0, width, height, factor );
    CHECK( width == 1920 );
    CHECK( height == 1080 );
    CHECK( factor == 1 );

    Moonlight::ComputeRenderSize( 1920, 1080, 0.5f, UpscaleFilter::Sharp, 0.f, 0, width, height, factor );
    CHECK( width == 960 );
    CHECK( height == 540 );

    // Out-of-range scales clamp (never above the output, never below 1 px).
    Moonlight::ComputeRenderSize( 1280, 720, 2.f, UpscaleFilter::Bilinear, 0.f, 0, width, height, factor );
    CHECK( width == 1280 );
    Moonlight::ComputeRenderSize( 8, 8, 0.01f, UpscaleFilter::Bilinear, 0.f, 0, width, height, factor );
    CHECK( width >= 1 );

    // A quality preset's ceiling, and an enforced profile's resolution (1280x720 at most).
    Moonlight::ComputeRenderSize( 1920, 1080, 1.f, UpscaleFilter::Bilinear, 0.75f, 0, width, height, factor );
    CHECK( width == 1440 );
    CHECK( height == 810 );
    Moonlight::ComputeRenderSize( 1920, 1080, 1.f, UpscaleFilter::Bilinear, 0.f, 1280ull * 720ull, width, height, factor );
    CHECK( width == 1280 );
    CHECK( height == 720 );
    Moonlight::ComputeRenderSize( 1280, 720, 1.f, UpscaleFilter::Bilinear, 0.f, 1920ull * 1080ull, width, height, factor );
    CHECK( width == 1280 );

    // Pixel: the scale snaps to 1/N and rounds up, so whole blocks cover the output.
    Moonlight::ComputeRenderSize( 1280, 720, 0.25f, UpscaleFilter::Pixel, 0.f, 0, width, height, factor );
    CHECK( factor == 4 );
    CHECK( width == 320 );
    CHECK( height == 180 );
    Moonlight::ComputeRenderSize( 1281, 721, 0.3f, UpscaleFilter::Pixel, 0.f, 0, width, height, factor );
    CHECK( factor == 4 );
    CHECK( width == 321 );
    CHECK( height == 181 );
    Moonlight::ComputeRenderSize( 1280, 720, 1.f, UpscaleFilter::Pixel, 0.f, 0, width, height, factor );
    CHECK( factor == 1 );
    CHECK( width == 1280 );
}


TEST_CASE( "Render scale: dynamic resolution steps toward the GPU target and holds near it" )
{
    // Over budget: down at once, toward the target's area ratio (20 ms vs 16 ms -> ~0.89).
    float scale = Moonlight::StepDynamicScale( 1.f, 20.f, 16.f, 0.5f, 1.f );
    CHECK( scale < 1.f );
    CHECK( scale >= 0.85f );
    // Far over: floored at the minimum, in 0.05 steps.
    scale = Moonlight::StepDynamicScale( 0.6f, 100.f, 16.f, 0.5f, 1.f );
    CHECK( scale == doctest::Approx( 0.5f ) );
    // Comfortably under: up one step; in the band: held.
    CHECK( Moonlight::StepDynamicScale( 0.5f, 8.f, 16.f, 0.5f, 1.f ) == doctest::Approx( 0.55f ) );
    CHECK( Moonlight::StepDynamicScale( 0.7f, 15.f, 16.f, 0.5f, 1.f ) == doctest::Approx( 0.7f ) );
    // Never above the ceiling (the camera's RenderScale).
    CHECK( Moonlight::StepDynamicScale( 0.75f, 1.f, 16.f, 0.5f, 0.75f ) == doctest::Approx( 0.75f ) );
}


TEST_CASE( "Render scale: MaxRenderScale caps parse and combine (stricter wins)" )
{
    HardwareProfile profile;
    profile.Deserialize( json::parse( R"({ "Name": "Low", "Caps": { "MaxRenderScale": 0.75 } })" ) );
    CHECK( profile.Caps.MaxRenderScale == doctest::Approx( 0.75f ) );
    HardwareProfile::FeatureCaps other;
    CHECK( HardwareProfile::Combine( profile.Caps, other ).MaxRenderScale == doctest::Approx( 0.75f ) );
    other.MaxRenderScale = 0.5f;
    CHECK( HardwareProfile::Combine( profile.Caps, other ).MaxRenderScale == doctest::Approx( 0.5f ) );
}


TEST_CASE( "Pixel-perfect: whole factors over the reference resolution, texel-grid snapping" )
{
    uint32_t factor = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    // 320x180 art: 4x in 1280x720, 6x in 1920x1080, 4x in a Steam Deck's 1280x800 (more world shown).
    Moonlight::ComputePixelPerfect( 1280, 720, 320, 180, factor, width, height );
    CHECK( factor == 4 );
    CHECK( width == 320 );
    CHECK( height == 180 );
    Moonlight::ComputePixelPerfect( 1920, 1080, 320, 180, factor, width, height );
    CHECK( factor == 6 );
    Moonlight::ComputePixelPerfect( 1280, 800, 320, 180, factor, width, height );
    CHECK( factor == 4 );
    CHECK( width == 320 );
    CHECK( height == 200 );
    // Not a multiple: the blocks still cover the output; smaller than the reference: 1x.
    Moonlight::ComputePixelPerfect( 1000, 600, 320, 180, factor, width, height );
    CHECK( factor == 3 );
    CHECK( width == 334 );
    CHECK( height == 200 );
    Moonlight::ComputePixelPerfect( 200, 100, 320, 180, factor, width, height );
    CHECK( factor == 1 );
    CHECK( width == 200 );

    // The same block size through the renderer's sizing (RenderScale 1/N, Pixel filter).
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t renderFactor = 0;
    Moonlight::ComputeRenderSize( 1000, 600, 1.f / 3.f, UpscaleFilter::Pixel, 0.f, 0, renderWidth, renderHeight, renderFactor );
    CHECK( renderFactor == 3 );
    CHECK( renderWidth == 334 );
    CHECK( renderHeight == 200 );

    // Snapping lands the eye's view-space x / y on the texel grid, leaving depth alone.
    const float texel = 1.f / 16.f;
    const Vector3 snapped = Moonlight::SnapToTexelGrid( Vector3( 1.01f, 2.49f, -10.f ), Vector3( 1.f, 0.f, 0.f ), Vector3( 0.f, 1.f, 0.f ), texel );
    CHECK( snapped.x == doctest::Approx( 1.0f ) );
    CHECK( snapped.y == doctest::Approx( 2.5f ) );
    CHECK( snapped.z == doctest::Approx( -10.f ) );
    // A sub-texel move keeps the same snapped eye: the image doesn't change.
    const Vector3 nudged = Moonlight::SnapToTexelGrid( Vector3( 1.01f + 0.2f * texel, 2.49f, -10.f ), Vector3( 1.f, 0.f, 0.f ), Vector3( 0.f, 1.f, 0.f ), texel );
    CHECK( nudged.x == doctest::Approx( snapped.x ) );
}
