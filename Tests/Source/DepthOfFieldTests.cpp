#include <doctest/doctest.h>
#include "Camera/DepthOfField.h"
#include "Components/Graphics/PostProcess.h"
#include <cmath>

TEST_CASE( "Depth of field: thin lens focuses on a plane and uses signed reciprocal depth" )
{
    Moonlight::PostProcessSettings settings;
    settings.DoFFocusDistance = 10.f;
    settings.DoFAperture = 2.f;
    // 50mm focal length on a 24mm-high filmback.
    const auto lens = Moonlight::MakeDepthOfFieldLens( settings, 2.f * 50.f / 24.f, 1080 );
    CHECK( lens.Radius( 10.f ) == doctest::Approx( 0.f ) );
    CHECK( lens.Radius( 20.f ) == doctest::Approx( 1.4133166f ) );
    CHECK( lens.Radius( 20.f / 3.f ) == doctest::Approx( -lens.Radius( 20.f ) ) );
    CHECK( lens.Radius( 5.f ) < 0.f );
    CHECK( lens.Radius( 100.f ) > 0.f );
}

TEST_CASE( "Depth of field: f-stops, filmback and resolution preserve optical relationships" )
{
    Moonlight::PostProcessSettings settings;
    const auto open = Moonlight::MakeDepthOfFieldLens( settings, 3.f, 1080 );
    settings.DoFAperture *= 2.f;
    const auto stopped = Moonlight::MakeDepthOfFieldLens( settings, 3.f, 1080 );
    CHECK( stopped.Radius( 16.f ) == doctest::Approx( open.Radius( 16.f ) * 0.5f ) );
    const auto uhd = Moonlight::MakeDepthOfFieldLens( settings, 3.f, 2160 );
    CHECK( uhd.Radius( 16.f ) == doctest::Approx( stopped.Radius( 16.f ) * 2.f ) );
    CHECK( uhd.MaxRadius == doctest::Approx( stopped.MaxRadius * 2.f ) );
    settings.DoFSensorHeight *= 2.f;
    const auto larger = Moonlight::MakeDepthOfFieldLens( settings, 3.f, 1080 );
    CHECK( larger.Radius( 16.f ) > stopped.Radius( 16.f ) );
}

TEST_CASE( "Depth of field: invalid lens ranges stay finite and blur work is bounded" )
{
    Moonlight::PostProcessSettings settings;
    settings.DoFFocusDistance = 0.f;
    settings.DoFAperture = 0.f;
    settings.DoFSensorHeight = -1.f;
    settings.DoFMaxBlurRadius = 1000.f;
    const auto lens = Moonlight::MakeDepthOfFieldLens( settings, 200.f, 8640 );
    CHECK( std::isfinite( lens.Radius( 0.f ) ) );
    CHECK( std::isfinite( lens.Radius( 100.f ) ) );
    CHECK( lens.MaxRadius == 64.f );
    CHECK( std::abs( lens.Radius( 0.f ) ) <= 64.f );
    settings.DoFMaxBlurRadius = 0.f;
    CHECK( Moonlight::MakeDepthOfFieldLens( settings, 3.f, 1080 ).Radius( 20.f ) == 0.f );
}

TEST_CASE( "Depth of field: existing cameras stay disabled and authored settings survive serialization" )
{
    PostProcess original;
    CHECK_FALSE( original.DepthOfField );
    CHECK_FALSE( original.DoFSplitDiopter );
    original.Deserialize( json{ { "Type", "PostProcess" }, { "Bloom", false } } );
    CHECK_FALSE( original.ToSettings().DepthOfField );
    CHECK_FALSE( original.ToSettings().DoFSplitDiopter );
    original.DepthOfField = true;
    original.DoFFocusDistance = 3.5f;
    original.DoFAperture = 1.4f;
    original.DoFSensorHeight = 36.f;
    original.DoFMaxBlurRadius = 24.f;
    original.DoFDebugCoC = true;
    original.DoFSplitDiopter = true;
    original.DoFSplitFocusDistance = 18.f;
    original.DoFSplitPosition = 0.42f;
    original.DoFSplitFeather = 0.025f;
    json saved;
    original.Serialize( saved );
    PostProcess restored;
    restored.Deserialize( saved );
    const auto settings = restored.ToSettings();
    CHECK( settings.DepthOfField );
    CHECK( settings.DoFFocusDistance == 3.5f );
    CHECK( settings.DoFAperture == 1.4f );
    CHECK( settings.DoFSensorHeight == 36.f );
    CHECK( settings.DoFMaxBlurRadius == 24.f );
    CHECK( settings.DoFDebugCoC );
    CHECK( settings.DoFSplitDiopter );
    CHECK( settings.DoFSplitFocusDistance == 18.f );
    CHECK( settings.DoFSplitPosition == 0.42f );
    CHECK( settings.DoFSplitFeather == 0.025f );
}
