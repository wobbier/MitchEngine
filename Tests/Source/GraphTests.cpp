#include <doctest/doctest.h>
#include <Widgets/Graph.h>
#include <Widgets/Color.h>
#include <Widgets/LightWidgets.h>
#include <Widgets/ItemLog.h>
#include <imgui.h>
#include <array>
#include <string>
#include <cmath>

using namespace MeWidgets;

namespace
{
    Graph::Mapping MakeMapping( Graph::Axis InX, Graph::Axis InY )
    {
        Graph::Mapping map;
        map.Min = ImVec2( 100.f, 50.f );
        map.Max = ImVec2( 300.f, 150.f );
        map.X = InX;
        map.Y = InY;
        return map;
    }
}

TEST_CASE( "Graph: linear axes map to the rect, Y up, and back" )
{
    const Graph::Mapping map = MakeMapping( { 0.0, 10.0 }, { 0.0, 1.0 } );
    CHECK( map.XToScreen( 0.0 ) == doctest::Approx( 100.f ) );
    CHECK( map.XToScreen( 10.0 ) == doctest::Approx( 300.f ) );
    CHECK( map.XToScreen( 2.5 ) == doctest::Approx( 150.f ) );
    // Y grows upwards: the axis minimum is the rect's bottom.
    CHECK( map.YToScreen( 0.0 ) == doctest::Approx( 150.f ) );
    CHECK( map.YToScreen( 1.0 ) == doctest::Approx( 50.f ) );
    for( double x : { 0.0, 1.25, 7.5, 10.0 } )
    {
        CHECK( map.XFromScreen( map.XToScreen( x ) ) == doctest::Approx( x ).epsilon( 1e-5 ) );
    }
    for( double y : { 0.0, 0.3, 1.0 } )
    {
        CHECK( map.YFromScreen( map.YToScreen( y ) ) == doctest::Approx( y ).epsilon( 1e-5 ) );
    }
}

TEST_CASE( "Graph: log X puts each decade the same width apart and inverts exactly" )
{
    const Graph::Mapping map = MakeMapping( { 1.0, 10000.0, true }, { 0.0, 1.0 } );
    // Four decades over 200 px: 50 px each.
    CHECK( map.XToScreen( 1.0 ) == doctest::Approx( 100.f ) );
    CHECK( map.XToScreen( 10.0 ) == doctest::Approx( 150.f ) );
    CHECK( map.XToScreen( 100.0 ) == doctest::Approx( 200.f ) );
    CHECK( map.XToScreen( 10000.0 ) == doctest::Approx( 300.f ) );
    for( double x : { 1.0, 3.7, 1900.0, 10000.0 } )
    {
        CHECK( map.XFromScreen( map.XToScreen( x ) ) == doctest::Approx( x ).epsilon( 1e-4 ) );
    }
    // Off the rect clamps to the axis ends.
    CHECK( map.XFromScreen( 0.f ) == doctest::Approx( 1.0 ) );
    CHECK( map.XFromScreen( 400.f ) == doctest::Approx( 10000.0 ) );

    // A log axis starting at zero is shifted: 0 is on the axis and maps back.
    const Graph::Mapping zero = MakeMapping( { 0.0, 10.0, true }, { 0.0, 1.0 } );
    CHECK( zero.XToScreen( 0.0 ) == doctest::Approx( 100.f ) );
    CHECK( zero.XFromScreen( 100.f ) == doctest::Approx( 0.0 ).epsilon( 1e-6 ) );
    CHECK( zero.XFromScreen( zero.XToScreen( 1.0 ) ) == doctest::Approx( 1.0 ).epsilon( 1e-4 ) );
    CHECK( zero.XToScreen( 1.0 ) > 200.f );
}

TEST_CASE( "Graph: ticks are round values inside the axis, thinned to fit" )
{
    double ticks[32];
    const int linear = Graph::Ticks( { 0.0, 12.0 }, ticks, 8 );
    REQUIRE( linear >= 3 );
    CHECK( linear <= 8 );
    CHECK( ticks[0] == doctest::Approx( 0.0 ) );
    for( int i = 0; i < linear; ++i )
    {
        CHECK( ticks[i] >= 0.0 );
        CHECK( ticks[i] <= 12.0 );
        CHECK( std::fmod( ticks[i], 1.0 ) == doctest::Approx( 0.0 ) );
    }
    const int log = Graph::Ticks( { 1000.0, 12000.0, true }, ticks, 8 );
    REQUIRE( log >= 2 );
    CHECK( ticks[0] == doctest::Approx( 1000.0 ) );
    CHECK( ticks[1] == doctest::Approx( 2000.0 ) );
    const int few = Graph::Ticks( { 0.001, 1000.0, true }, ticks, 4 );
    CHECK( few <= 4 );
    CHECK( few >= 2 );
}

TEST_CASE( "Graph: angles around an apex" )
{
    const ImVec2 apex( 0.f, 0.f );
    const float down = 3.14159265f * 0.5f;
    const ImVec2 below = Graph::ArcPoint( apex, down, 0.f, 10.f );
    CHECK( below.x == doctest::Approx( 0.f ).epsilon( 1e-4 ) );
    CHECK( below.y == doctest::Approx( 10.f ) );
    // Negative angles from "down" are on the right of the screen.
    const ImVec2 right = Graph::ArcPoint( apex, down, -3.14159265f / 6.f, 10.f );
    CHECK( right.x > 0.f );
    CHECK( Graph::AngleOf( apex, down, right ) == doctest::Approx( -3.14159265f / 6.f ).epsilon( 1e-4 ) );
}

TEST_CASE( "Colour temperature: blackbody colours and the way back" )
{
    using namespace ColorTemperature;
    const auto daylight = ToSrgb( 6500.0 );
    CHECK( daylight[0] > 0.97f );
    CHECK( daylight[1] > 0.97f );
    CHECK( daylight[2] > 0.95f );

    const auto candle = ToSrgb( 1900.0 );
    CHECK( candle[0] == doctest::Approx( 1.f ) );
    CHECK( candle[1] == doctest::Approx( 0.517f ).epsilon( 0.01 ) );
    CHECK( candle[2] < 0.05f );

    const auto sky = ToSrgb( 10000.0 );
    CHECK( sky[2] == doctest::Approx( 1.f ) );
    CHECK( sky[0] < 0.85f );
    CHECK( sky[0] < sky[1] );

    for( double kelvin : { 1200.0, 1900.0, 2700.0, 3200.0, 5600.0, 6500.0, 7500.0, 10000.0, 11500.0 } )
    {
        double found = 0.0;
        REQUIRE( FromSrgb( ToSrgb( kelvin ), found ) );
        CHECK( found == doctest::Approx( kelvin ).epsilon( 0.005 ) );
    }
    // Scaled colours are the same temperature; colours off the curve aren't one.
    const auto warm = ToSrgb( 3200.0 );
    double scaled = 0.0;
    CHECK( FromSrgb( { warm[0] * 0.5f, warm[1] * 0.5f, warm[2] * 0.5f }, scaled ) );
    CHECK( scaled == doctest::Approx( 3200.0 ).epsilon( 0.005 ) );
    double off = 0.0;
    CHECK_FALSE( FromSrgb( { 0.2f, 1.f, 0.2f }, off ) );
    CHECK_FALSE( FromSrgb( { 1.f, 0.f, 1.f }, off ) );
    CHECK_FALSE( FromSrgb( { 0.f, 0.f, 0.f }, off ) );
}

TEST_CASE( "Light falloff: windowed inverse square reaches zero at the range" )
{
    CHECK( LightAttenuation( 1.0, 100.0 ) == doctest::Approx( 1.0 ).epsilon( 1e-6 ) );
    CHECK( LightAttenuation( 2.0, 100.0 ) == doctest::Approx( 0.25 ).epsilon( 1e-4 ) );
    CHECK( LightAttenuation( 10.0, 10.0 ) == doctest::Approx( 0.0 ) );
    CHECK( LightAttenuation( 12.0, 10.0 ) == doctest::Approx( 0.0 ) );
    CHECK( LightAttenuation( 5.0, 10.0 ) < 1.0 / 25.0 );
    // The near field keeps extended emitters finite on their surface.
    CHECK( LightAttenuation( 0.0, 10.0, 0.5 ) == doctest::Approx( 2.0 ) );
}

TEST_CASE( "Light widgets: parts are named items in their widget's row, reporting what they show" )
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    struct Cleanup
    {
        ImGuiContext* Context;
        ImGuiContext* Previous;
        ~Cleanup() { ItemLog::Enable( false ); ImGui::DestroyContext( Context ); ImGui::SetCurrentContext( Previous ); }
    } cleanup{ context, previous };
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2( 1280.f, 900.f );
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );
    ItemLog::Enable( true );

    const std::array<float, 3> start = ColorTemperature::ToSrgb( 5600.0 );
    float color[3] = { start[0], start[1], start[2] };
    double intensity = 1.0;
    double inner = 20.0;
    double outer = 30.0;
    double range = 10.0;
    NumberSpec spec;
    spec.FieldUnit = Unit::Multiplier;
    spec.HasRange = true;
    spec.Min = 0.0;
    spec.Max = 100.0;
    spec.HasSoftRange = true;
    spec.SoftMax = 10.0;
    spec.Log = true;
    NumberSpec meters;
    meters.FieldUnit = Unit::Meters;
    meters.HasRange = true;
    meters.Min = 0.01;
    meters.Max = 1000.0;
    const Reference rungs[] = { { "Candle", 0.1, "" }, { "Bulb", 1.0, "" } };
    auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos( ImVec2( 10.f, 10.f ) );
        ImGui::SetNextWindowSize( ImVec2( 600.f, 860.f ) );
        ImGui::Begin( "Probe" );
        KelvinBar( "Temperature", color );
        ReferenceLadder( "Reference", intensity, spec, rungs, 2, "tip" );
        ConeWedge( "Cone", inner, outer, color );
        FalloffGraph( "Falloff", range, meters, color );
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();

    std::string error;
    const ItemLog::Item* candle = ItemLog::Find( "Probe/Temperature/Candle", error );
    REQUIRE_MESSAGE( candle, error );
    CHECK( candle->Value == "1900 K" );
    const ItemLog::Item* bar = ItemLog::Find( "Probe/Temperature", error );
    REQUIRE_MESSAGE( bar, error );
    CHECK( bar->Value == "5600 K" );
    const ItemLog::Item* bulb = ItemLog::Find( "Reference/Bulb", error );
    REQUIRE_MESSAGE( bulb, error );
    const ItemLog::Item* innerHandle = ItemLog::Find( "Cone/Inner Cone", error );
    REQUIRE_MESSAGE( innerHandle, error );
    CHECK( innerHandle->Value == "20°" );
    const ItemLog::Item* outerHandle = ItemLog::Find( "Cone/Outer Cone", error );
    REQUIRE_MESSAGE( outerHandle, error );
    CHECK( outerHandle->Value == "30°" );
    const ItemLog::Item* rangeHandle = ItemLog::Find( "Falloff/Range", error );
    REQUIRE_MESSAGE( rangeHandle, error );
    CHECK( rangeHandle->Value == "10 m" );
}
