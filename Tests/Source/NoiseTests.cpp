#include <doctest/doctest.h>

#include "Math/Hash.h"
#include "Math/Noise.h"
#include <cmath>
#include <vector>

TEST_CASE( "Noise: PCG32 matches the reference generator" )
{
    // pcg32-demo: pcg32_srandom_r( &rng, 42u, 54u ).
    Pcg32 rng( 42u, 54u );
    const uint32_t expected[] = { 0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu };
    for( uint32_t value : expected )
    {
        CHECK( rng.Next() == value );
    }

    Pcg32 bounded( 7u );
    for( int i = 0; i < 1000; ++i )
    {
        CHECK( bounded.Below( 13u ) < 13u );
        const float unit = bounded.Unit();
        CHECK( unit >= 0.f );
        CHECK( unit < 1.f );
    }
    CHECK( Hash::Of( 1, 2, 3 ) == Hash::Of( 1, 2, 3 ) );
    CHECK( Hash::Of( 1, 2, 3 ) != Hash::Of( 1, 3, 2 ) );
}


TEST_CASE( "Noise: gradient noise is bounded, seeded and zero on the lattice" )
{
    Pcg32 rng( 99u );
    float lowest = 0.f, highest = 0.f;
    for( int i = 0; i < 20000; ++i )
    {
        const float x = rng.Range( -500.f, 500.f ), y = rng.Range( -500.f, 500.f );
        const float value = Noise::Value2( x, y, 3u );
        lowest = std::min( lowest, value );
        highest = std::max( highest, value );
        const float value3 = Noise::Value3( x, y, x * 0.37f, 3u );
        CHECK( std::fabs( value3 ) <= 1.05f );
    }
    CHECK( lowest >= -1.001f );
    CHECK( highest <= 1.001f );
    // Covers most of the range (not a degenerate generator).
    CHECK( lowest < -0.6f );
    CHECK( highest > 0.6f );
    CHECK( Noise::Value2( 12.f, -7.f, 3u ) == doctest::Approx( 0.f ) );
    CHECK( Noise::Value2( 0.37f, 0.61f, 3u ) != Noise::Value2( 0.37f, 0.61f, 4u ) );
}


TEST_CASE( "Noise: analytic derivatives match finite differences" )
{
    Pcg32 rng( 5u );
    const float h = 1e-3f;
    Noise::Fractal fractal;
    fractal.Octaves = 5;
    for( int i = 0; i < 200; ++i )
    {
        const float x = rng.Range( -50.f, 50.f ), y = rng.Range( -50.f, 50.f );
        const Noise::Sample2 sample = Noise::Gradient2( x, y, 11u );
        const float fdx = ( Noise::Value2( x + h, y, 11u ) - Noise::Value2( x - h, y, 11u ) ) / ( 2.f * h );
        const float fdy = ( Noise::Value2( x, y + h, 11u ) - Noise::Value2( x, y - h, 11u ) ) / ( 2.f * h );
        CHECK( sample.Dx == doctest::Approx( fdx ).epsilon( 0.02 ).scale( 1.0 ) );
        CHECK( sample.Dy == doctest::Approx( fdy ).epsilon( 0.02 ).scale( 1.0 ) );

        float dx = 0.f, dy = 0.f;
        Noise::Fbm2( x * 0.1f, y * 0.1f, 11u, fractal, &dx, &dy );
        const float sx = x * 0.1f, sy = y * 0.1f, step = 1e-4f;
        const float ex = ( Noise::Fbm2( sx + step, sy, 11u, fractal ) - Noise::Fbm2( sx - step, sy, 11u, fractal ) ) / ( 2.f * step );
        const float ey = ( Noise::Fbm2( sx, sy + step, 11u, fractal ) - Noise::Fbm2( sx, sy - step, 11u, fractal ) ) / ( 2.f * step );
        CHECK( dx == doctest::Approx( ex ).epsilon( 0.05 ).scale( 1.0 ) );
        CHECK( dy == doctest::Approx( ey ).epsilon( 0.05 ).scale( 1.0 ) );
    }
}


TEST_CASE( "Noise: periodic variants tile" )
{
    Noise::Fractal fractal;
    fractal.Octaves = 4;
    Pcg32 rng( 21u );
    for( int i = 0; i < 500; ++i )
    {
        const float x = rng.Range( 0.f, 8.f ), y = rng.Range( 0.f, 4.f );
        CHECK( Noise::GradientPeriodic2( x, y, 8, 4, 9u ).Value == doctest::Approx( Noise::GradientPeriodic2( x + 8.f, y - 4.f, 8, 4, 9u ).Value ).epsilon( 1e-3 ).scale( 1.0 ) );
        CHECK( Noise::FbmPeriodic2( x, y, 8, 4, 9u, fractal ) == doctest::Approx( Noise::FbmPeriodic2( x - 8.f, y + 4.f, 8, 4, 9u, fractal ) ).epsilon( 1e-3 ).scale( 1.0 ) );
        const Noise::Cell a = Noise::Voronoi2( x, y, 9u, 1.f, 8, 4 ), b = Noise::Voronoi2( x + 8.f, y + 4.f, 9u, 1.f, 8, 4 );
        CHECK( a.F1 == doctest::Approx( b.F1 ).epsilon( 1e-3 ).scale( 1.0 ) );
        CHECK( a.Id == b.Id );
    }
}


TEST_CASE( "Noise: fractals stay in range and Voronoi orders its distances" )
{
    Noise::Fractal fractal;
    Pcg32 rng( 77u );
    for( int i = 0; i < 5000; ++i )
    {
        const float x = rng.Range( -100.f, 100.f ), y = rng.Range( -100.f, 100.f );
        const float fbm = Noise::Fbm2( x, y, 1u, fractal );
        CHECK( std::fabs( fbm ) <= 1.f );
        const float ridged = Noise::Ridged2( x, y, 1u, fractal );
        CHECK( ridged >= 0.f );
        CHECK( ridged <= 1.f );
        const float eroded = Noise::ErodedFbm2( x, y, 1u, fractal );
        CHECK( std::fabs( eroded ) <= 1.f );
        const Noise::Cell cell = Noise::Voronoi2( x, y, 1u );
        CHECK( cell.F1 <= cell.F2 );
        CHECK( cell.F1 < 1.5f );
    }
}


TEST_CASE( "Noise: hybrid multifractal detail grows with altitude" )
{
    // The defining property for terrain: the roughness added by the high octaves correlates with
    // the height of the low-frequency base, so valleys stay smooth and peaks get rough.
    Pcg32 rng( 1234u );
    std::vector<float> base, detail;
    for( int i = 0; i < 4000; ++i )
    {
        const float x = rng.Range( -200.f, 200.f ), y = rng.Range( -200.f, 200.f );
        const float coarse = Noise::HybridMultifractal2( x, y, 8u, 2, 2.f, 0.25f, 0.7f );
        const float fine = Noise::HybridMultifractal2( x, y, 8u, 8, 2.f, 0.25f, 0.7f );
        base.push_back( coarse );
        detail.push_back( std::fabs( fine - coarse ) );
    }
    double meanBase = 0, meanDetail = 0;
    for( size_t i = 0; i < base.size(); ++i ) { meanBase += base[i]; meanDetail += detail[i]; }
    meanBase /= static_cast<double>( base.size() );
    meanDetail /= static_cast<double>( base.size() );
    double covariance = 0, varianceBase = 0, varianceDetail = 0;
    for( size_t i = 0; i < base.size(); ++i )
    {
        covariance += ( base[i] - meanBase ) * ( detail[i] - meanDetail );
        varianceBase += ( base[i] - meanBase ) * ( base[i] - meanBase );
        varianceDetail += ( detail[i] - meanDetail ) * ( detail[i] - meanDetail );
    }
    const double correlation = covariance / std::sqrt( varianceBase * varianceDetail );
    MESSAGE( "altitude/roughness correlation " << correlation );
    CHECK( correlation > 0.3 );
}


TEST_CASE( "Noise: outputs stay stable (generated content depends on them)" )
{
    // Saved terrains, trees and scatters regenerate from seeds: changing these values changes every
    // procedural asset. Update them only for an intentional, documented noise revision.
    Noise::Fractal fractal;
    CHECK( Noise::Value2( 0.37f, 0.61f, 3u ) == doctest::Approx( -0.3491753f ).epsilon( 1e-5 ) );
    CHECK( Noise::Value3( 1.25f, -3.5f, 0.75f, 3u ) == doctest::Approx( -0.3375998f ).epsilon( 1e-5 ) );
    CHECK( Noise::Fbm2( 1.3f, -2.7f, 1u, fractal ) == doctest::Approx( -0.1513005f ).epsilon( 1e-4 ) );
    CHECK( Noise::Ridged2( 4.1f, 0.2f, 1u, fractal ) == doctest::Approx( 0.5324126f ).epsilon( 1e-4 ) );
    CHECK( Noise::HybridMultifractal2( -3.3f, 5.5f, 8u, 8, 2.f, 0.25f, 0.7f ) == doctest::Approx( 1.1300948f ).epsilon( 1e-4 ) );
    CHECK( Noise::ErodedFbm2( 2.2f, 2.9f, 1u, fractal ) == doctest::Approx( -0.0563414f ).epsilon( 1e-3 ) );
    const Noise::Cell cell = Noise::Voronoi2( 7.7f, -1.2f, 1u );
    CHECK( cell.F1 == doctest::Approx( 0.4035068f ).epsilon( 1e-5 ) );
    CHECK( cell.F2 == doctest::Approx( 0.5721180f ).epsilon( 1e-5 ) );
    CHECK( cell.Id == 3494594777u );
}
