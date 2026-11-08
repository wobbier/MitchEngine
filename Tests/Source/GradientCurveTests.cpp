#include <doctest/doctest.h>
#include <Widgets/GradientWidgets.h>
#include <Widgets/CurveWidgets.h>
#include <Widgets/ItemLog.h>
#include <cmath>
#include <string>
#include <vector>

using namespace MeWidgets;

namespace
{
    GradientMath::Stop MakeStop( float InPosition, float InR, float InG, float InB, float InAlpha )
    {
        GradientMath::Stop stop;
        stop.Position = InPosition;
        stop.Color = { InR, InG, InB };
        stop.Alpha = InAlpha;
        return stop;
    }

    // The particle defaults: StartColor (1, 0.6, 0.2) at 100%, EndColor (0.6, 0.15, 0.05) at 0%.
    std::vector<GradientMath::Stop> ParticleDefaults()
    {
        return { MakeStop( 0.f, 1.f, 0.6f, 0.2f, 1.f ), MakeStop( 1.f, 0.6f, 0.15f, 0.05f, 0.f ) };
    }
}

TEST_CASE( "Gradient: evaluation blends sRGB linearly, as the particle runtime does" )
{
    const std::vector<GradientMath::Stop> stops = ParticleDefaults();
    for( float life : { 0.f, 0.25f, 0.5f, 0.9f, 1.f } )
    {
        // ParticleSystem::FillBatch: srgb = mix( Start, End, life ); alpha = mix( StartAlpha, EndAlpha, life ).
        const GradientMath::Sample sample = GradientMath::Evaluate( stops, life );
        CHECK( sample.Color[0] == doctest::Approx( 1.f + ( 0.6f - 1.f ) * life ) );
        CHECK( sample.Color[1] == doctest::Approx( 0.6f + ( 0.15f - 0.6f ) * life ) );
        CHECK( sample.Color[2] == doctest::Approx( 0.2f + ( 0.05f - 0.2f ) * life ) );
        CHECK( sample.Alpha == doctest::Approx( 1.f - life ) );
    }
    // The ends hold beyond the outer stops.
    const std::vector<GradientMath::Stop> inner = { MakeStop( 0.25f, 0.f, 0.f, 0.f, 1.f ), MakeStop( 0.75f, 1.f, 1.f, 1.f, 0.5f ) };
    CHECK( GradientMath::Evaluate( inner, 0.f ).Color[0] == doctest::Approx( 0.f ) );
    CHECK( GradientMath::Evaluate( inner, 1.f ).Alpha == doctest::Approx( 0.5f ) );
    CHECK( GradientMath::Evaluate( inner, 0.5f ).Color[1] == doctest::Approx( 0.5f ) );
    // Stops out of order evaluate in position order; no stops is opaque white.
    const std::vector<GradientMath::Stop> shuffled = { inner[1], inner[0] };
    CHECK( GradientMath::Evaluate( shuffled, 0.5f ).Color[1] == doctest::Approx( 0.5f ) );
    CHECK( GradientMath::Evaluate( {}, 0.3f ).Alpha == doctest::Approx( 1.f ) );
    // Two stops at one position make a hard edge.
    const std::vector<GradientMath::Stop> edge = { MakeStop( 0.f, 0.f, 0.f, 0.f, 1.f ), MakeStop( 0.5f, 0.f, 0.f, 0.f, 1.f ), MakeStop( 0.5f, 1.f, 1.f, 1.f, 1.f ), MakeStop( 1.f, 1.f, 1.f, 1.f, 1.f ) };
    CHECK( GradientMath::Evaluate( edge, 0.49f ).Color[0] == doctest::Approx( 0.f ) );
    CHECK( GradientMath::Evaluate( edge, 0.51f ).Color[0] == doctest::Approx( 1.f ) );
}

TEST_CASE( "Gradient: add, remove, sort and snap" )
{
    std::vector<GradientMath::Stop> stops = ParticleDefaults();
    const GradientMath::Sample before = GradientMath::Evaluate( stops, 0.3f );
    const int added = GradientMath::AddStop( stops, 0.5f );
    CHECK( added == 1 );
    REQUIRE( stops.size() == 3 );
    CHECK( stops[1].Position == doctest::Approx( 0.5f ) );
    // Sampled from the gradient: adding a stop changes nothing you can see.
    CHECK( stops[1].Alpha == doctest::Approx( 0.5f ) );
    CHECK( GradientMath::Evaluate( stops, 0.3f ).Color[1] == doctest::Approx( before.Color[1] ) );
    CHECK( GradientMath::Evaluate( stops, 0.3f ).Alpha == doctest::Approx( before.Alpha ) );
    CHECK( GradientMath::AddStop( stops, 2.f ) == 3 );
    CHECK( stops[3].Position == doctest::Approx( 1.f ) );

    // Removal stops at the minimum.
    CHECK( GradientMath::RemoveStop( stops, 3, 2 ) );
    CHECK( GradientMath::RemoveStop( stops, 1, 2 ) );
    CHECK_FALSE( GradientMath::RemoveStop( stops, 0, 2 ) );
    CHECK_FALSE( GradientMath::RemoveStop( stops, 5, 0 ) );
    CHECK( stops.size() == 2 );

    // A drag can leave a stop past its neighbour: Sort puts them back and follows the stop.
    stops[0].Position = 0.9f;
    stops[1].Position = 0.3f;
    const int tracked = GradientMath::Sort( stops, 0 );
    CHECK( tracked == 1 );
    CHECK( stops[0].Position == doctest::Approx( 0.3f ) );
    CHECK( stops[1].Position == doctest::Approx( 0.9f ) );
    CHECK( GradientMath::Sort( stops, -1 ) == -1 );

    // Dragged positions: whole percent (Shift: a tenth), snapped near the quarters.
    CHECK( GradientMath::RoundPosition( 0.4567f, false ) == doctest::Approx( 0.46f ) );
    CHECK( GradientMath::RoundPosition( 0.4567f, true ) == doctest::Approx( 0.457f ) );
    CHECK( GradientMath::RoundPosition( -0.2f, false ) == 0.f );
    CHECK( GradientMath::SnapToDetent( 0.26f, 0.02f ) == doctest::Approx( 0.25f ) );
    CHECK( GradientMath::SnapToDetent( 0.3f, 0.02f ) == doctest::Approx( 0.3f ) );
    CHECK( GradientMath::SnapToDetent( 0.99f, 0.02f ) == doctest::Approx( 1.f ) );
    int detents = 0;
    GradientMath::Detents( detents );
    CHECK( detents == 5 );
}

TEST_CASE( "Gradient: reports what it shows" )
{
    const std::vector<GradientMath::Stop> stops = ParticleDefaults();
    CHECK( GradientMath::DescribeStop( stops[0] ) == "#FF9933 100%" );
    CHECK( GradientMath::Describe( stops ) == "#FF9933 100% -> #99260D 0%" );
    std::vector<GradientMath::Stop> three = { stops[1], stops[0], MakeStop( 0.5f, 0.f, 0.f, 1.f, 0.5f ) };
    CHECK( GradientMath::Describe( three ) == "#FF9933 100% -> #0000FF 50% -> #99260D 0%" );
}

TEST_CASE( "Curve: Hermite evaluation, tangent modes and presets" )
{
    using namespace CurveMath;
    // Linear keys: a straight line, held beyond the ends.
    std::vector<Key> line = MakePreset( Preset::Linear, 0.0, 1.0, 1.0, 0.5 );
    REQUIRE( line.size() == 2 );
    CHECK( Evaluate( line, 0.0 ) == doctest::Approx( 1.0 ) );
    CHECK( Evaluate( line, 0.5 ) == doctest::Approx( 0.75 ) );
    CHECK( Evaluate( line, 1.0 ) == doctest::Approx( 0.5 ) );
    CHECK( Evaluate( line, -1.0 ) == doctest::Approx( 1.0 ) );
    CHECK( Evaluate( line, 2.0 ) == doctest::Approx( 0.5 ) );
    CHECK( Evaluate( {}, 0.5 ) == 0.0 );

    // Ease In is t^2, Ease Out 1 - (1 - t)^2, Ease In-Out smoothstep, Constant the start value.
    const std::vector<Key> easeIn = MakePreset( Preset::EaseIn, 0.0, 1.0, 0.0, 1.0 );
    const std::vector<Key> easeOut = MakePreset( Preset::EaseOut, 0.0, 1.0, 0.0, 1.0 );
    const std::vector<Key> easeInOut = MakePreset( Preset::EaseInOut, 0.0, 1.0, 0.0, 1.0 );
    const std::vector<Key> constant = MakePreset( Preset::Constant, 0.0, 1.0, 0.25, 1.0 );
    for( double t : { 0.1, 0.3, 0.5, 0.8 } )
    {
        CHECK( Evaluate( easeIn, t ) == doctest::Approx( t * t ) );
        CHECK( Evaluate( easeOut, t ) == doctest::Approx( 1.0 - ( 1.0 - t ) * ( 1.0 - t ) ) );
        CHECK( Evaluate( easeInOut, t ) == doctest::Approx( t * t * ( 3.0 - 2.0 * t ) ) );
        CHECK( Evaluate( constant, t ) == doctest::Approx( 0.25 ) );
    }
    // Over another span the shape scales with it.
    const std::vector<Key> wide = MakePreset( Preset::EaseIn, 2.0, 4.0, 10.0, 20.0 );
    CHECK( Evaluate( wide, 3.0 ) == doctest::Approx( 12.5 ) );
    CHECK( std::string( PresetName( Preset::EaseInOut ) ) == "Ease In-Out" );

    // Auto: a peak stays level, a slope takes the neighbours' secant, the ends their segment's.
    std::vector<Key> keys( 3 );
    keys[0] = { 0.0, 0.0 };
    keys[1] = { 0.5, 1.0 };
    keys[2] = { 1.0, 0.0 };
    UpdateTangents( keys );
    CHECK( keys[1].In == doctest::Approx( 0.0 ) );
    CHECK( keys[1].Out == doctest::Approx( 0.0 ) );
    CHECK( keys[0].Out == doctest::Approx( 2.0 ) );
    CHECK( keys[2].In == doctest::Approx( -2.0 ) );
    keys[2].Y = 2.0;
    UpdateTangents( keys );
    CHECK( keys[1].Out == doctest::Approx( 2.0 ) );
    // Auto never overshoots its neighbours: monotone keys give a monotone curve.
    std::vector<Key> steps( 3 );
    steps[0] = { 0.0, 0.0 };
    steps[1] = { 0.1, 0.9 };
    steps[2] = { 1.0, 1.0 };
    UpdateTangents( steps );
    double previous = -1.0;
    for( int i = 0; i <= 100; ++i )
    {
        const double y = Evaluate( steps, i / 100.0 );
        CHECK( y >= previous - 1e-9 );
        CHECK( y <= 1.0 + 1e-9 );
        previous = y;
    }

    // Linear: straight to each neighbour (a kink at the key); Flat: level; Free: kept.
    keys[1].Mode = Tangent::Linear;
    UpdateTangents( keys );
    CHECK( keys[1].In == doctest::Approx( 2.0 ) );
    CHECK( keys[1].Out == doctest::Approx( 2.0 ) );
    keys[2].Y = 0.0;
    UpdateTangents( keys );
    CHECK( keys[1].Out == doctest::Approx( -2.0 ) );
    CHECK( Evaluate( keys, 0.75 ) == doctest::Approx( 0.5 ) );
    keys[1].Mode = Tangent::Flat;
    UpdateTangents( keys );
    CHECK( keys[1].In == 0.0 );
    keys[1].Mode = Tangent::Free;
    keys[1].In = 3.0;
    keys[1].Out = -1.0;
    UpdateTangents( keys );
    CHECK( keys[1].In == 3.0 );
    CHECK( keys[1].Out == -1.0 );
    CHECK( std::string( TangentName( Tangent::Free ) ) == "Free" );
}

TEST_CASE( "Curve: keys are added in order and removed down to the minimum" )
{
    using namespace CurveMath;
    std::vector<Key> keys = MakePreset( Preset::Linear, 0.0, 1.0, 0.0, 1.0 );
    CHECK( AddKey( keys, 0.5, 0.9 ) == 1 );
    REQUIRE( keys.size() == 3 );
    CHECK( keys[1].Mode == Tangent::Auto );
    CHECK( Evaluate( keys, 0.5 ) == doctest::Approx( 0.9 ) );
    CHECK( AddKey( keys, 0.25, 0.1 ) == 1 );
    CHECK( keys[2].X == doctest::Approx( 0.5 ) );
    CHECK( RemoveKey( keys, 1, 2 ) );
    CHECK( RemoveKey( keys, 1, 2 ) );
    CHECK_FALSE( RemoveKey( keys, 0, 2 ) );
    CHECK( keys.size() == 2 );
    CHECK( Describe( keys, Unit::Percent, Unit::Multiplier ) == "(0%, 0\xC3\x97) (100%, 1\xC3\x97)" );
    CHECK( DescribeKey( keys[1], Unit::Percent, Unit::None ) == "100%, 1" );
}

TEST_CASE( "ItemLog: parts of a row scope queries like rows" )
{
    ItemLog::Item handle;
    handle.Label = "In";
    handle.Row = "Curve";
    handle.Part = "Key 2";
    handle.Window = "Widget Gallery";
    handle.NeedsRowScope = true;
    CHECK( ItemLog::Matches( handle, ItemLog::Parse( "Curve/Key 2/In" ) ) );
    CHECK( ItemLog::Matches( handle, ItemLog::Parse( "Widget Gallery/Key 2/In" ) ) );
    CHECK( ItemLog::Matches( handle, ItemLog::Parse( "Curve/In" ) ) );
    CHECK_FALSE( ItemLog::Matches( handle, ItemLog::Parse( "Curve/Key 3/In" ) ) );
    CHECK_FALSE( ItemLog::Matches( handle, ItemLog::Parse( "In" ) ) );
    CHECK_FALSE( ItemLog::Matches( handle, ItemLog::Parse( "Curve/Key 2" ) ) );
}
