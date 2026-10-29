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
