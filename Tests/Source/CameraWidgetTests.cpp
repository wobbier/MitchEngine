#include <doctest/doctest.h>
#include <Widgets/CameraWidgets.h>
#include <cmath>

using namespace MeWidgets;

TEST_CASE( "Camera widgets: field of view and focal length invert each other" )
{
    // A 50 mm lens on full frame (24 mm tall) sees 26.99 degrees vertically.
    CHECK( Lens::VerticalFov( 50.0, 24.0 ) == doctest::Approx( 26.9915 ).epsilon( 1e-4 ) );
    CHECK( Lens::FocalLength( 26.9915, 24.0 ) == doctest::Approx( 50.0 ).epsilon( 1e-4 ) );
    // 35 mm: 2 atan(24 / 70).
    CHECK( Lens::VerticalFov( 35.0, 24.0 ) == doctest::Approx( 2.0 * std::atan( 24.0 / 70.0 ) * 180.0 / 3.14159265358979 ) );
    // 90 degrees: the focal length is half the sensor height.
    CHECK( Lens::FocalLength( 90.0, 24.0 ) == doctest::Approx( 12.0 ) );
    for( double fov : { 1.0, 20.0, 60.0, 120.0, 179.0 } )
    {
        CHECK( Lens::VerticalFov( Lens::FocalLength( fov, 18.0 ), 18.0 ) == doctest::Approx( fov ) );
    }
    // A smaller sensor needs a shorter lens for the same view.
    CHECK( Lens::FocalLength( 40.0, 12.0 ) == doctest::Approx( Lens::FocalLength( 40.0, 24.0 ) * 0.5 ) );
}

TEST_CASE( "Camera widgets: depth of field limits follow the hyperfocal formula" )
{
    CHECK( Lens::CircleOfConfusion( 24.0 ) == doctest::Approx( 0.03 ) );
    CHECK( Lens::CircleOfConfusion( 12.0 ) == doctest::Approx( 0.015 ) );

    // 50 mm at f/2.8, c = 0.03 mm: H = 50^2 / (2.8 * 0.03) + 50 = 29811.9 mm.
    const Lens::FocusRange tenMetres = Lens::DepthOfField( 50.0, 2.8, 10.0, 0.03 );
    CHECK( tenMetres.Hyperfocal == doctest::Approx( 29.8119 ).epsilon( 1e-4 ) );
    CHECK( tenMetres.Near == doctest::Approx( 7.4945 ).epsilon( 1e-3 ) );
    CHECK( tenMetres.Far == doctest::Approx( 15.0222 ).epsilon( 1e-3 ) );
    CHECK( tenMetres.Near < 10.0 );
    CHECK( tenMetres.Far > 10.0 );

    // Stopping down widens the band; a longer lens narrows it.
    const Lens::FocusRange stopped = Lens::DepthOfField( 50.0, 8.0, 10.0, 0.03 );
    CHECK( stopped.Near < tenMetres.Near );
    CHECK( stopped.Far > tenMetres.Far );
    const Lens::FocusRange longer = Lens::DepthOfField( 135.0, 2.8, 10.0, 0.03 );
    CHECK( longer.Far - longer.Near < tenMetres.Far - tenMetres.Near );

    // Focused at the hyperfocal distance: sharp from half of it to infinity.
    const Lens::FocusRange hyper = Lens::DepthOfField( 50.0, 2.8, tenMetres.Hyperfocal, 0.03 );
    CHECK( hyper.Near == doctest::Approx( tenMetres.Hyperfocal * 0.5 ).epsilon( 2e-3 ) );
    CHECK( std::isinf( hyper.Far ) );
    CHECK( std::isinf( Lens::DepthOfField( 50.0, 2.8, 100.0, 0.03 ).Far ) );

    // Focus inside the focal length stays finite.
    const Lens::FocusRange close = Lens::DepthOfField( 50.0, 2.8, 0.01, 0.03 );
    CHECK( std::isfinite( close.Near ) );
}

TEST_CASE( "Camera widgets: f-numbers snap to nominal third stops" )
{
    CHECK( Lens::SnapThirdStop( 2.8 ) == 2.8 );
    CHECK( Lens::SnapThirdStop( 2.7 ) == 2.8 );
    CHECK( Lens::SnapThirdStop( 3.3 ) == 3.2 );
    CHECK( Lens::SnapThirdStop( 12.5 ) == 13.0 );
    CHECK( Lens::SnapThirdStop( 0.1 ) == 0.7 );
    CHECK( Lens::SnapThirdStop( 100.0 ) == 32.0 );
    int count = 0;
    const double* thirds = Lens::ThirdStops( count );
    REQUIRE( count > 0 );
    // Every nominal third stop is within 6% of the exact 2^(k/6) series.
    for( int i = 0; i < count; ++i )
    {
        const double k = std::round( 6.0 * std::log2( thirds[i] ) );
        CHECK( std::abs( thirds[i] / std::exp2( k / 6.0 ) - 1.0 ) < 0.06 );
        if( i > 0 )
        {
            CHECK( thirds[i] > thirds[i - 1] );
        }
    }
    int full = 0;
    const double* fulls = Lens::FullStops( full );
    CHECK( full == 9 );
    CHECK( fulls[0] == 1.4 );
    CHECK( fulls[full - 1] == 22.0 );
}
